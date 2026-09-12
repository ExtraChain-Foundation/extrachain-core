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
#include "dfs/dfs_utils.h"
#include "dfs/dirs_manager.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"
#include "utils/exc_utils.h"

namespace {
    using namespace std::chrono_literals;

    constexpr std::size_t TOTAL_ROWS = 40;

    // A peer that owns the complete vector and serves snapshots of the first
    // `served_rows` of them, so a partial answer can be simulated exactly.
    class VectorPeer final : public SocketService {
    public:
        VectorPeer(ExtraChain::Core::ExtraChainNode &node,
                   ActorId                           owner,
                   std::string                       file_id,
                   Dfs::CollectionTemplate           collection_template,
                   std::vector<DbRow>                rows,
                   std::string                       vector_file)
            : SocketService(*node.network())
            , node_(node)
            , owner_(std::move(owner))
            , file_id_(std::move(file_id))
            , template_(std::move(collection_template))
            , rows_(std::move(rows))
            , vector_file_(std::move(vector_file)) {
            identifier_ = std::string(64, 'f');
            activated_  = true;
        }

        std::atomic_uint      requests { 0 };
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
            if (type != MessageType::DfsFileRequest && type != MessageType::DfsFileState) {
                return;
            }
            if (type == MessageType::DfsFileState) {
                // Answer "I have it, Ready" so the node keeps us as a source.
                Responder responder;
                responder.add_identifier(identifier_);
                boost::asio::post(node_.serial_executor(), [this, responder] {
                    node_.dfs()->network_response_file_state(
                        { .owner_id = owner_, .file_id = file_id_, .state = Dfs::FileState::Ready }, responder);
                });
                return;
            }
            requests.fetch_add(1);
            const auto take = served_rows.load();
            boost::asio::post(node_.serial_executor(), [this, take] {
                std::vector<DbRow> content(rows_.begin(), rows_.begin() + static_cast<std::ptrdiff_t>(take));
                node_.dfs()->network_response_content_vector(
                    Dfs::Packets::DfsVectorContentPackage { .owner_id        = owner_,
                                                            .file_id         = file_id_,
                                                            .vector_template = template_,
                                                            .vector_file     = vector_file_,
                                                            .content         = std::move(content) });
            });
        }

    private:
        ExtraChain::Core::ExtraChainNode &node_;
        ActorId                           owner_;
        std::string                       file_id_;
        Dfs::CollectionTemplate           template_;
        std::vector<DbRow>                rows_;
        std::string                       vector_file_;
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
    const std::string mode = argv[1];

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

    for (std::size_t index = 0; index < TOTAL_ROWS; ++index) {
        DbRow entry;
        entry["id"]       = "row_" + std::to_string(index);
        entry["payload"]  = "payload_" + std::to_string(index);
        entry["position"] = std::to_string(index);
        TEST_REQUIRE(node->dfs()->add_vector_row(owner_id, file_id, entry));
    }

    const auto all_rows = node->dfs()->read_vector_rows(owner_id, file_id);
    TEST_REQUIRE(all_rows.has_value());
    TEST_REQUIRE_EQ(all_rows->size(), TOTAL_ROWS);

    const auto rows_now = [&]() -> std::size_t {
        const auto rows = node->dfs()->read_vector_rows(owner_id, file_id);
        return rows.has_value() ? rows->size() : 0;
    };
    const auto catalog_hash = [&]() {
        const auto row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(),
                                                                        owner_id,
                                                                        file_id);
        TEST_REQUIRE(row.has_value());
        return row->hash;
    };
    const auto complete_hash = catalog_hash();

    // The companion template file travels with a snapshot.
    const auto vector_path = Dfs::Path::file_path(owner_id, file_id);
    TEST_REQUIRE(vector_path.has_value());
    std::string companion;
    if (const auto content = Utils::read_file_content(
            FsPath::create(vector_path->native().string() + ".vector").value());
        content.has_value()) {
        companion = ByteArray(content.value()).toString();
    }

    // Now make the local copy partial: keep the first half, drop the rest, and
    // leave the catalog row claiming the complete content (that is what a node
    // that missed gossiped rows looks like).
    const std::size_t kept = TOTAL_ROWS / 2;
    {
        DbConnector db(vector_path->native());
        TEST_REQUIRE(db.open(false));
        for (std::size_t index = kept; index < TOTAL_ROWS; ++index) {
            TEST_REQUIRE(db.query("DELETE FROM Vector WHERE id = 'row_" + std::to_string(index) + "'"));
        }
        db.close();
    }
    TEST_REQUIRE_EQ(rows_now(), kept);
    TEST_REQUIRE(catalog_hash() == complete_hash);
    // A partial copy must not look complete to the completeness check.
    TEST_REQUIRE(!node->dfs()->is_file_already_downloaded(owner_id, file_id, complete_hash));

    auto peer = std::make_shared<VectorPeer>(*node, owner_id, file_id, vector_template, all_rows.value(), companion);
    node->network()->connections()->insert(peer);

    if (mode == "converge") {
        peer->served_rows = TOTAL_ROWS;
        node->dfs()->request_vector_content(owner_id, file_id);
        TEST_REQUIRE(wait_for([&] { return rows_now() == TOTAL_ROWS; }));
        TEST_REQUIRE(node->dfs()->is_file_already_downloaded(owner_id, file_id, complete_hash));
        std::printf("vector replication converge: %zu rows after %u request(s)\n",
                    rows_now(),
                    peer->requests.load());
    } else if (mode == "staged") {
        // The peer is itself short at first, exactly like a node that is still
        // publishing. The copy must keep asking and converge once the peer is full.
        peer->served_rows = kept + 5;
        node->dfs()->request_vector_content(owner_id, file_id);
        TEST_REQUIRE(wait_for([&] { return rows_now() == kept + 5; }));
        TEST_REQUIRE(wait_for([&] { return peer->requests.load() >= 2; }));
        peer->served_rows = TOTAL_ROWS;
        TEST_REQUIRE(wait_for([&] { return rows_now() == TOTAL_ROWS; }, 60s));
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
        TEST_REQUIRE(wait_for([&] { return peer->requests.load() >= 1; }));
        std::this_thread::sleep_for(30s);
        const auto attempts = peer->requests.load();
        std::printf("vector replication bounded: %u request(s) in 30s, rows=%zu\n", attempts, rows_now());
        TEST_REQUIRE_MESSAGE(attempts <= 6, "repair must stop asking a source that cannot help");
    }

    node->cleanUp();
    node.reset();
    std::error_code ignored;
    std::filesystem::current_path(std::filesystem::temp_directory_path(), ignored);
    std::filesystem::remove_all(directory, ignored);
    std::printf("PASS\n");
    return 0;
}
