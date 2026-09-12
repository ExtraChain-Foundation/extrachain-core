// Vector replication, measured directly instead of through a soak run.
//
// A node holds a partial copy of somebody else's vector; a fake peer holds the
// complete one and answers content requests with a real snapshot. The question
// this test answers is the one the stand could not: does a short copy converge,
// and does it stop asking once it has?
//
// Modes:
//   converge  — the peer always answers in full; the copy must reach every row.
//   staged    — the first answers are themselves short (what a mid-publication
//               peer sends); the copy must still converge once a full answer
//               arrives, and must keep asking until then.
//   bounded   — the peer never has more than the node does; the node must stop
//               asking instead of looping forever.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/post.hpp>

#include "chain/actor.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/vector_sync.h"
#include "dfs/dfs_utils.h"
#include "dfs/dirs_manager.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"
#include "utils/exc_utils.h"

namespace {
    using namespace std::chrono_literals;

    // A peer that owns the complete vector and serves snapshots of the first
    // `served_rows` of them, so a partial answer can be simulated exactly.
    class VectorPeer final : public SocketService {
    public:
        VectorPeer(ExtraChain::Core::ExtraChainNode &node,
                   ActorId                           owner,
                   std::string                       file_id,
                   Dfs::CollectionTemplate           collection_template,
                   std::vector<DbRow>                rows,
                   std::string                       vector_file,
                   char                              source = 'f')
            : SocketService(*node.network())
            , node_(node)
            , owner_(std::move(owner))
            , file_id_(std::move(file_id))
            , template_(std::move(collection_template))
            , rows_(std::move(rows))
            , vector_file_(std::move(vector_file)) {
            identifier_      = std::string(64, source);
            activated_       = true;
            const auto  path = Dfs::Path::file_path(owner_, file_id_).value();
            DbConnector source_database(path);
            TEST_REQUIRE(source_database.open(false));
            const auto schema = source_database.select("SELECT sql FROM sqlite_master WHERE name='Vector'");
            TEST_REQUIRE(schema.size() == 1);
            peer_path_ =
                FsPath::create(std::filesystem::current_path() / (std::string("peer-vector-") + source + ".db"))
                    .value();
            DbConnector database(peer_path_);
            TEST_REQUIRE(database.open());
            TEST_REQUIRE(database.query(schema.front().at("sql")));
            TEST_REQUIRE(database.query("BEGIN IMMEDIATE"));
            for (const auto &row : rows_)
                TEST_REQUIRE(database.replace("Vector", row));
            Dfs::VectorIndex index(database, "id");
            TEST_REQUIRE(index.root().has_value());
            TEST_REQUIRE(database.query("COMMIT"));
            stored_rows_ = rows_.size();
        }

        std::atomic_bool         silent { false };
        std::atomic_uint         requests { 0 };
        std::atomic_size_t       transferred_rows { 0 };
        std::atomic_bool         tamper_once { false };
        std::atomic_bool         tampered { false };
        std::atomic<std::size_t> served_rows { 0 };

        std::string protocol_string() const override {
            return "test";
        }
        Network::Protocol protocol() const override {
            return Network::Protocol::WebSocket;
        }
        bool is_active() const override {
            return activated_.load();
        }
        std::uint16_t port() const override {
            return 0;
        }
        std::uint16_t server_port() const override {
            return 0;
        }
        void flush() override {
        }

        void send_message(std::span<const std::uint8_t> data, Priority) override {
            constexpr std::size_t signature_bytes = 64;
            if (data.size() <= signature_bytes) {
                return;
            }
            const auto message = MessagePack::deserialize<MessageBody>(
                std::string(reinterpret_cast<const char *>(data.data()), data.size() - signature_bytes));
            if (!message.has_value()) {
                return;
            }
            const auto type = message.value().message_type;
            if (type != MessageType::DfsVectorSyncRequest && type != MessageType::DfsFileState) {
                return;
            }
            if (type == MessageType::DfsFileState) {
                // Answer "I have it, Ready" so the node keeps us as a source.
                Responder responder;
                responder.add_identifier(identifier_);
                boost::asio::post(node_.serial_executor(), [this, responder] {
                    node_.dfs()->network_response_file_state({ .owner_id = owner_,
                                                               .file_id  = file_id_,
                                                               .state    = Dfs::FileState::Ready },
                                                             responder);
                });
                return;
            }
            const auto decoded = MessagePack::deserialize<Dfs::VectorSyncRequest>(message.value().data);
            TEST_REQUIRE(decoded.has_value());
            const auto     &request = decoded.value();
            std::lock_guard lock(snapshot_mutex_);
            if (silent) {
                requests.fetch_add(1);
                return;
            }
            if (request.release) {
                snapshot_.reset();
                return;
            }
            Dfs::VectorSyncReply reply { .link = { owner_, file_id_ } };
            if (request.snapshot.empty()) {
                requests.fetch_add(1);
                snapshot_.reset();
                const auto take = served_rows.load();
                if (take != stored_rows_) {
                    DbConnector database(peer_path_);
                    TEST_REQUIRE(database.open(false));
                    TEST_REQUIRE(database.query("BEGIN IMMEDIATE"));
                    TEST_REQUIRE(database.query("DELETE FROM Vector"));
                    for (std::size_t i = 0; i < take; ++i)
                        TEST_REQUIRE(database.replace("Vector", rows_[i]));
                    Dfs::VectorIndex index(database, "id");
                    TEST_REQUIRE(index.root().has_value());
                    TEST_REQUIRE(database.query("COMMIT"));
                    stored_rows_ = take;
                }
                auto snapshot = Dfs::VectorSnapshot::open(peer_path_, "id");
                TEST_REQUIRE(snapshot.has_value());
                snapshot_      = std::move(snapshot.value());
                snapshot_id_   = Utils::generate_random_hex(64);
                reply.metadata = Dfs::Packets::DfsVectorContentPackage { .owner_id        = owner_,
                                                                         .file_id         = file_id_,
                                                                         .vector_template = template_,
                                                                         .vector_file     = vector_file_ };
            } else {
                TEST_REQUIRE(request.snapshot == snapshot_id_ && snapshot_);
                const auto slice = snapshot_->read(request.prefix);
                TEST_REQUIRE(slice.has_value());
                reply.slice = slice.value();
                transferred_rows.fetch_add(slice.value().rows.size());
                if (tamper_once.exchange(false) && !reply.slice.value().rows.empty()) {
                    reply.slice.value().rows.front()["payload"] = "tampered";
                    tampered.store(true);
                }
            }
            reply.snapshot     = snapshot_id_;
            reply.root         = snapshot_->root();
            const auto encoded = MessagePack::serialize(reply);
            Responder  responder;
            responder.add_identifier(identifier_);
            responder.set_message_id(message.value().message_id);
            Responder wrong_peer;
            wrong_peer.add_identifier(std::string(64, 'e'));
            wrong_peer.set_message_id(responder.message_id());
            TEST_REQUIRE(!node_.dfs()->vector_sync().receive_reply(encoded, wrong_peer));
            auto wrong_request = responder.with_new_message_id();
            TEST_REQUIRE(!node_.dfs()->vector_sync().receive_reply(encoded, wrong_request));
            TEST_REQUIRE(node_.dfs()->vector_sync().receive_reply(encoded, responder));
            TEST_REQUIRE(!node_.dfs()->vector_sync().receive_reply(encoded, responder));
        }

    private:
        ExtraChain::Core::ExtraChainNode    &node_;
        ActorId                              owner_;
        std::string                          file_id_;
        Dfs::CollectionTemplate              template_;
        std::vector<DbRow>                   rows_;
        std::string                          vector_file_;
        FsPath                               peer_path_;
        std::mutex                           snapshot_mutex_;
        std::unique_ptr<Dfs::VectorSnapshot> snapshot_;
        std::string                          snapshot_id_;
        std::size_t                          stored_rows_ = 0;
    };

    template <typename Predicate>
    bool wait_for(Predicate predicate, std::chrono::seconds limit = 20s) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(20ms);
        }
        return predicate();
    }
} // namespace

int main(int argc, char **argv) {
    TEST_REQUIRE(argc == 2);
    const std::string mode       = argv[1];
    const std::size_t TOTAL_ROWS = mode == "large" ? 100001 : (mode == "delta" ? 2048 : 40);

    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vec-repl-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);

    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("vec-repl", ActorType::User, owner);
    node->dfs()->set_mode(DfsMode::Full);

    const auto owner_id = owner.id();

    // Build the complete vector locally first: this is the material the fake peer
    // will serve back. Rows are signed by the real actor, so they verify.
    auto collection_template = Dfs::CollectionTemplate::create("replicated");
    TEST_REQUIRE(collection_template.has_value());
    auto vector_template = collection_template.value().use_id().add_fields(
        { Dfs::Field::String("payload").not_null(), Dfs::Field::Integer("position").not_null() });
    const auto stored_template = node->dfs()->store_template(owner_id, vector_template);
    TEST_REQUIRE(stored_template.has_value());
    const auto vector =
        node->dfs()->store_vector(owner_id, owner_id, "replicated", owner_id, stored_template->file_id);
    TEST_REQUIRE(vector.has_value());
    const auto file_id = vector->file_id;

    auto dfs_vector = node->dfs()->make_vector(owner_id, file_id).value().second;
    auto package    = dfs_vector.generate_content_package_empty().value();
    for (std::size_t index = 0; index < TOTAL_ROWS; ++index) {
        DbRow entry { { "id", "row_" + std::to_string(index) },
                      { "payload",
                        (mode == "large" ? std::string(1024, 'p') : std::string("payload_"))
                            + std::to_string(index) },
                      { "position", std::to_string(index) } };
        if (mode != "large") {
            TEST_REQUIRE(node->dfs()->add_vector_row(owner_id, file_id, entry));
            continue;
        }
        entry["timestamp"] = std::to_string(Utils::current_date_ms());
        entry["status"]    = "1";
        entry["actor"]     = owner_id.to_string();
        const auto hash    = dfs_vector.calculate_hash(entry);
        TEST_REQUIRE(!hash.first.empty());
        const auto sign = owner.key().sign(hash.first);
        TEST_REQUIRE(sign.has_value());
        entry["sign"] = ByteArray(sign.value()).toString();
        package.content.push_back(std::move(entry));
        if (package.content.size() == 256 || index + 1 == TOTAL_ROWS) {
            TEST_REQUIRE(dfs_vector.handle_package(package));
            package.content.clear();
        }
    }
    if (mode == "large") {
        const auto complete = dfs_vector.index_root().value();
        TEST_REQUIRE(complete.tree.bytes > 64ULL * 1024 * 1024);
        TEST_REQUIRE(node->dfs()->get_db_instance()->update(Dfs::Tables::DirsFile::TableNameActorsFiles,
                                                            { { "hash", complete.hash },
                                                              { "size", std::to_string(complete.tree.bytes) } },
                                                            { { "owner_id", owner_id.to_string() },
                                                              { "file_id", file_id } }));
    }

    const auto all_rows = node->dfs()->read_vector_rows(owner_id, file_id);
    TEST_REQUIRE(all_rows.has_value());
    TEST_REQUIRE_EQ(all_rows->size(), TOTAL_ROWS);

    const auto rows_now = [&]() -> std::size_t {
        const auto root = dfs_vector.index_root();
        return root.has_value() ? root.value().tree.rows : 0;
    };
    const auto catalog_hash = [&]() {
        const auto row =
            Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner_id, file_id);
        TEST_REQUIRE(row.has_value());
        return row->hash;
    };
    const auto complete_hash = catalog_hash();

    // The companion template file travels with a snapshot.
    const auto vector_path = Dfs::Path::file_path(owner_id, file_id);
    TEST_REQUIRE(vector_path.has_value());
    std::string companion;
    if (const auto content =
            Utils::read_file_content(FsPath::create(vector_path->native().string() + ".vector").value());
        content.has_value()) {
        companion = ByteArray(content.value()).toString();
    }

    // Now make the local copy partial: keep the first half, drop the rest, and
    // leave the catalog row claiming the complete content (that is what a node
    // that missed gossiped rows looks like).
    const std::size_t kept = mode == "large" ? 0 : (mode == "delta" ? TOTAL_ROWS - 1 : TOTAL_ROWS / 2);
    {
        DbConnector db(vector_path->native());
        TEST_REQUIRE(db.open(false));
        TEST_REQUIRE(db.query("BEGIN IMMEDIATE"));
        for (std::size_t index = kept; index < TOTAL_ROWS; ++index) {
            TEST_REQUIRE(db.query("DELETE FROM Vector WHERE id = 'row_" + std::to_string(index) + "'"));
        }
        TEST_REQUIRE(db.query("COMMIT"));
        db.close();
    }
    TEST_REQUIRE_EQ(rows_now(), kept);
    TEST_REQUIRE(catalog_hash() == complete_hash);
    // A partial copy must not look complete to the completeness check.
    TEST_REQUIRE(!node->dfs()->is_file_already_downloaded(owner_id, file_id, complete_hash));

    auto peer =
        std::make_shared<VectorPeer>(*node, owner_id, file_id, vector_template, all_rows.value(), companion);
    node->network()->connections()->insert(peer);

    std::shared_ptr<VectorPeer> unavailable;
    if (mode == "failover") {
        unavailable         = std::make_shared<VectorPeer>(*node,
                                                           owner_id,
                                                           file_id,
                                                           vector_template,
                                                           all_rows.value(),
                                                           companion,
                                                           'e');
        unavailable->silent = true;
        node->network()->connections()->insert(unavailable);
    }

    if (mode == "tamper") {
        peer->served_rows = TOTAL_ROWS;
        peer->tamper_once = true;
        node->dfs()->request_vector_content(owner_id, file_id);
        TEST_REQUIRE(wait_for([&] {
            return peer->tampered.load();
        }));
        std::this_thread::sleep_for(500ms);
        TEST_REQUIRE_EQ(rows_now(), kept);
        TEST_REQUIRE(wait_for([&] {
            return rows_now() == TOTAL_ROWS;
        }));
        TEST_REQUIRE(node->dfs()->is_file_already_downloaded(owner_id, file_id, complete_hash));
    } else if (mode == "converge" || mode == "delta" || mode == "failover" || mode == "large") {
        peer->served_rows           = TOTAL_ROWS;
        const auto transfer_started = std::chrono::steady_clock::now();
        node->dfs()->request_vector_content(owner_id, file_id);
        TEST_REQUIRE(wait_for(
            [&] {
                return rows_now() == TOTAL_ROWS;
            },
            mode == "large" ? 180s : 20s));
        TEST_REQUIRE(node->dfs()->is_file_already_downloaded(owner_id, file_id, complete_hash));
        if (mode == "failover")
            TEST_REQUIRE(unavailable->requests.load() > 0);
        if (mode == "delta") {
            TEST_REQUIRE(peer->transferred_rows.load() > 0);
            TEST_REQUIRE(peer->transferred_rows.load() <= 256);
        }
        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - transfer_started).count();
        std::printf(
            "vector replication converge: %zu rows, %llu bytes after %u snapshot(s), %zu transferred rows in %.2f "
            "s\n",
            rows_now(),
            static_cast<unsigned long long>(dfs_vector.index_root().value().tree.bytes),
            peer->requests.load(),
            peer->transferred_rows.load(),
            elapsed);
    } else if (mode == "staged") {
        // The peer is itself short at first, exactly like a node that is still
        // publishing. The copy must keep asking and converge once the peer is full.
        peer->served_rows = kept + 5;
        node->dfs()->request_vector_content(owner_id, file_id);
        TEST_REQUIRE(wait_for([&] {
            return rows_now() == kept + 5;
        }));
        TEST_REQUIRE(wait_for([&] {
            return peer->requests.load() >= 2;
        }));
        peer->served_rows = TOTAL_ROWS;
        TEST_REQUIRE(wait_for(
            [&] {
                return rows_now() == TOTAL_ROWS;
            },
            60s));
        TEST_REQUIRE(node->dfs()->is_file_already_downloaded(owner_id, file_id, complete_hash));
        std::printf("vector replication staged: %zu rows after %u request(s)\n",
                    rows_now(),
                    peer->requests.load());
    } else {
        TEST_REQUIRE(mode == "bounded");
        // The peer never has more than we do: the repair must give up rather than
        // pull snapshots without end (unbounded retries OOM-killed stand nodes).
        peer->served_rows = kept;
        node->dfs()->request_vector_content(owner_id, file_id);
        TEST_REQUIRE(wait_for([&] {
            return peer->requests.load() >= 1;
        }));
        std::this_thread::sleep_for(30s);
        const auto attempts = peer->requests.load();
        std::printf("vector replication bounded: %u request(s) in 30s, rows=%zu\n", attempts, rows_now());
        TEST_REQUIRE_MESSAGE(attempts <= 6, "repair must stop asking a source that cannot help");
    }

    peer.reset();
    node->cleanUp();
    node.reset();
    std::error_code ignored;
    std::filesystem::current_path(std::filesystem::temp_directory_path(), ignored);
    std::filesystem::remove_all(directory, ignored);
    std::printf("PASS\n");
    return 0;
}
