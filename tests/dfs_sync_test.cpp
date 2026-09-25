#include <condition_variable>
#include <mutex>
// Content-based catalog sync (tracker #75) and the merge rules it depends on.
//
// Before: every handshake pulled the whole catalog (DfsTempSyncAll), the receiver
// INSERTed rows without checking signatures, a row the owner re-signed never
// replaced the local one, and a tombstone kept its pre-removal signature locally.
// Now: one digest per owner over (file_id, sign); only owners that differ travel;
// the merge verifies File/Folder rows and tombstones, takes newer re-signed rows,
// ignores older ones and forged ones.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "chain/actor.h"
#include "chain/actor_index.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/dfs_utils.h"
#include "dfs/dirs_manager.h"
#include "managers/account_controller.h"
#include "network/responder.h"
#include "test_support.h"
#include "utils/exc_utils.h"
#include "utils/hash.h"
#include "utils/serialization.h"

namespace {
    struct Capture : ResponseSender {
        mutable std::mutex                               mutex;
        std::condition_variable                          ready;
        std::vector<std::pair<MessageType, std::string>> sent;
        void                                             wait_messages(std::size_t count) {
            std::unique_lock lock(mutex);
            TEST_REQUIRE(ready.wait_for(lock, std::chrono::seconds(5), [&] {
                return sent.size() >= count;
            }));
        }

        std::string send_response(const std::string &data,
                                  MessageType        type,
                                  SendMode,
                                  MessageStatus,
                                  const Responder &) override {
            std::lock_guard lock(mutex);
            sent.emplace_back(type, data);
            ready.notify_all();
            return "captured";
        }

        std::size_t count(MessageType type) const {
            std::lock_guard lock(mutex);
            std::size_t n = 0;
            for (const auto &[sent_type, _] : sent) {
                n += sent_type == type;
            }
            return n;
        }

        std::string payload(MessageType type) const {
            std::lock_guard lock(mutex);
            for (const auto &[sent_type, data] : sent) {
                if (sent_type == type) {
                    return data;
                }
            }
            static const std::string none;
            return none;
        }
    };

    Dfs::DirRow file_row(const Actor<KeyPrivate> &owner,
                         const std::string       &file_id,
                         const std::string       &name,
                         std::uint64_t            stamp) {
        Dfs::DirRow row;
        row.actor_id          = owner.id();
        row.owner_id          = owner.id();
        row.file_id           = file_id;
        row.hash              = Utils::calculate_hash(name);
        row.name              = name;
        row.size              = 10;
        row.created           = 1;
        row.metadata_revision = stamp;
        row.last_modified     = stamp;
        row.type              = Dfs::FileType::File;
        row.state             = Dfs::FileState::Ready;
        const auto sign       = owner.key().sign(row.calculate_hash(owner.id()));
        TEST_REQUIRE(sign.has_value());
        row.sign = sign.value();
        return row;
    }

    // Exactly what DfsService::remove_stored_file signs and stores.
    Dfs::DirRow tombstone(const Actor<KeyPrivate> &signer, Dfs::DirRow row, std::uint64_t stamp) {
        row.hash              = "";
        row.folder            = std::nullopt;
        row.name              = "";
        row.size              = 0;
        row.state             = Dfs::FileState::Removed;
        row.last_modified     = stamp;
        row.metadata_revision = stamp;
        const auto sign       = signer.key().sign(row.calculate_hash(row.owner_id));
        TEST_REQUIRE(sign.has_value());
        row.sign = sign.value();
        return row;
    }

    const Dfs::Packets::CatalogDigest *digest_of(const std::vector<Dfs::Packets::CatalogDigest> &digests,
                                                 const ActorId                                  &owner) {
        for (const auto &digest : digests) {
            if (digest.owner_id == owner) {
                return &digest;
            }
        }
        return nullptr;
    }
} // namespace

int main() {
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-dfs-sync-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);

    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("dfs-sync-profile", ActorType::User, owner);

    // The remote catalog owner: known to this node, key never present here.
    Actor<KeyPrivate> peer;
    peer.create(ActorType::User);
    TEST_REQUIRE(node->actor_index()->save_actor(peer.to_public()).has_value());
    Actor<KeyPrivate> impostor;
    impostor.create(ActorType::User);

    auto     &dirs = node->dfs_service()->dirs_manager();
    Capture   requests;
    Responder from_peer(&requests);
    from_peer.add_identifier("peer-node");

    const auto rows_of = [&](const ActorId &who) -> std::size_t {
        const auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(dirs.get_db_instance(), who, 0);
        return rows.has_value() ? rows->size() : 0;
    };
    const auto row_of = [&](const ActorId &who, const std::string &file_id) {
        const auto row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs.get_db_instance(), who, file_id);
        TEST_REQUIRE(row.has_value());
        return row.value();
    };
    const auto merge = [&](const ActorId &who, std::vector<Dfs::DirRow> rows) {
        std::ranges::sort(rows, { }, &Dfs::DirRow::file_id);
        auto response = from_peer;
        response.set_message_id(dirs.request_catalog_rows({ .owners = { who } }, from_peer));
        TEST_REQUIRE(!response.message_id().empty());
        const auto before = node->dfs()->staged_startup_response_count();
        dirs.network_response_dir_rows(MessagePack::serialize(Dfs::CatalogRowsPage { .rows = std::move(rows) }),
                                       response);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (node->dfs()->staged_startup_response_count() == before
               && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        TEST_REQUIRE(node->dfs()->staged_startup_response_count() > before);
    };

    const std::string f1 = Utils::generate_random_hex(64);
    const std::string f2 = Utils::generate_random_hex(64);
    const std::string f3 = Utils::generate_random_hex(64);

    // 0. The node can verify the remote owner's signature the way the merge will.
    {
        auto       r0    = file_row(peer, f1, "probe.txt", 1);
        const auto known = node->actor_index()->read_actor(peer.id());
        TEST_REQUIRE(known.has_value());
        const auto ok = known->key().verify(r0.calculate_hash(peer.id()), r0.sign);
        TEST_REQUIRE(ok.has_value() && ok.value());
    }

    // 1. Two genuine rows land.
    const auto r1 = file_row(peer, f1, "a.txt", 1000);
    const auto r2 = file_row(peer, f2, "b.txt", 1000);
    merge(peer.id(), { r1, r2 });
    TEST_REQUIRE_EQ(rows_of(peer.id()), std::size_t(2));

    // 2. A row in the peer's name signed by someone else: rejected.
    auto forged = file_row(peer, f3, "forged.txt", 1000);
    forged.sign = impostor.key().sign(forged.calculate_hash(peer.id())).value();
    merge(peer.id(), { forged });
    TEST_REQUIRE_EQ(rows_of(peer.id()), std::size_t(2));

    // 3. The owner re-signed the row (rename): newer replaces, state is kept.
    const auto r1_renamed = file_row(peer, f1, "renamed.txt", 2000);
    merge(peer.id(), { r1_renamed });
    TEST_REQUIRE_EQ(row_of(peer.id(), f1).name, std::string("renamed.txt"));
    TEST_REQUIRE(row_of(peer.id(), f1).sign == r1_renamed.sign);

    // 4. An older re-signed version: ignored.
    merge(peer.id(), { file_row(peer, f1, "stale.txt", 500) });
    TEST_REQUIRE_EQ(row_of(peer.id(), f1).name, std::string("renamed.txt"));

    // 5. A genuine tombstone: state Removed, and the tombstone's signature is taken so
    //    both sides end up with the same (file_id, sign) pair.
    const auto t2 = tombstone(peer, r2, 3000);
    merge(peer.id(), { t2 });
    TEST_REQUIRE(row_of(peer.id(), f2).state == Dfs::FileState::Removed);
    TEST_REQUIRE(row_of(peer.id(), f2).sign == t2.sign);
    TEST_REQUIRE_EQ(row_of(peer.id(), f2).last_modified, std::uint64_t(3000));

    // 6. A tombstone signed by someone else: rejected, the file stays.
    merge(peer.id(), { tombstone(impostor, r1_renamed, 4000) });
    TEST_REQUIRE(row_of(peer.id(), f1).state != Dfs::FileState::Removed);
    TEST_REQUIRE_EQ(row_of(peer.id(), f1).name, std::string("renamed.txt"));

    // 7. Digests: one per owner, deterministic, and they follow the content.
    const auto digests     = dirs.catalog_digests();
    const auto peer_digest = digest_of(digests, peer.id());
    TEST_REQUIRE(peer_digest != nullptr);
    TEST_REQUIRE_EQ(peer_digest->rows, std::uint64_t(2));
    TEST_REQUIRE(!peer_digest->digest.empty());
    const auto again = dirs.catalog_digests({ peer.id() });
    TEST_REQUIRE_EQ(again.size(), std::size_t(1));
    TEST_REQUIRE(again.front().digest == peer_digest->digest);

    // Immutable file content cannot be changed through the local vector-hint API.
    {
        auto moved = row_of(peer.id(), f1);
        moved.hash = Utils::calculate_hash("unverified content");
        TEST_REQUIRE(!Dfs::Tables::DirsFile::ActorSpace::update_file_metadata(dirs.get_db_instance(),
                                                                              peer.id(),
                                                                              moved,
                                                                              false));
        TEST_REQUIRE_EQ(row_of(peer.id(), f1).hash, r1_renamed.hash);
    }

    // 8. A requester holding the same catalog: no rows travel, the reply is empty.
    {
        Capture   capture;
        Responder to_peer(&capture);
        to_peer.add_identifier(Utils::generate_random_hex(64));
        dirs.network_request_digest({ .owners = digests, .allowed = { } }, to_peer);
        capture.wait_messages(1);
        TEST_REQUIRE_EQ(capture.count(MessageType::DfsSyncDirRows), std::size_t(0));
        TEST_REQUIRE_EQ(capture.count(MessageType::DfsSyncDigestReply), std::size_t(1));
        const auto reply = MessagePack::deserialize<Dfs::Packets::CatalogDigestReply>(
            capture.payload(MessageType::DfsSyncDigestReply));
        TEST_REQUIRE(reply.has_value());
        TEST_REQUIRE(reply->mismatched.empty());
        TEST_REQUIRE(reply->unknown.empty());
    }

    // 9. A requester whose copy of the peer's catalog differs: exactly that owner's rows
    //    travel, and the reply carries our digest for it.
    {
        auto stale = digests;
        for (auto &digest : stale) {
            if (digest.owner_id == peer.id()) {
                digest.digest = std::string(64, '0');
            }
        }
        Capture   capture;
        Responder to_peer(&capture);
        to_peer.add_identifier(Utils::generate_random_hex(64));
        dirs.network_request_digest({ .owners = stale, .allowed = { } }, to_peer);
        capture.wait_messages(2);
        TEST_REQUIRE_EQ(capture.count(MessageType::DfsSyncDirRows), std::size_t(1));
        const auto request =
            MessagePack::deserialize<Dfs::CatalogRowsRequest>(capture.payload(MessageType::DfsSyncDirRows));
        TEST_REQUIRE(request.has_value());
        TEST_REQUIRE(request.value().owners == std::vector<ActorId> { peer.id() });
        const auto reply = MessagePack::deserialize<Dfs::Packets::CatalogDigestReply>(
            capture.payload(MessageType::DfsSyncDigestReply));
        TEST_REQUIRE(reply.has_value());
        TEST_REQUIRE_EQ(reply->mismatched.size(), std::size_t(1));
        TEST_REQUIRE(reply->mismatched.front().digest == peer_digest->digest);
    }

    // 10. A requester that knows nothing (empty digest list) gets every owner; one that
    //     lists an owner we have never seen is told so.
    {
        Actor<KeyPrivate> stranger;
        stranger.create(ActorType::User);
        Capture   capture;
        Responder to_peer(&capture);
        to_peer.add_identifier(Utils::generate_random_hex(64));
        dirs.network_request_digest({ .owners  = { { .owner_id = stranger.id(),
                                                     .rows     = 1,
                                                     .digest   = std::string(64, 'b') } },
                                      .allowed = { } },
                                    to_peer);
        capture.wait_messages(2);
        const auto request =
            MessagePack::deserialize<Dfs::CatalogRowsRequest>(capture.payload(MessageType::DfsSyncDirRows));
        TEST_REQUIRE(request.has_value());
        TEST_REQUIRE(request.value().owners == std::vector<ActorId> { stranger.id() });
        const auto reply = MessagePack::deserialize<Dfs::Packets::CatalogDigestReply>(
            capture.payload(MessageType::DfsSyncDigestReply));
        TEST_REQUIRE(reply.has_value());
        TEST_REQUIRE_EQ(reply->unknown.size(), std::size_t(1));
        TEST_REQUIRE(reply->unknown.front() == stranger.id());
    }

    // 11. A Selective-style request narrows the comparison to the allowed owners.
    {
        Capture   capture;
        Responder to_peer(&capture);
        to_peer.add_identifier(Utils::generate_random_hex(64));
        dirs.network_request_digest({ .owners = { }, .allowed = { peer.id() } }, to_peer);
        capture.wait_messages(1);
        TEST_REQUIRE_EQ(capture.count(MessageType::DfsSyncDirRows), std::size_t(0));
        const auto reply = MessagePack::deserialize<Dfs::Packets::CatalogDigestReply>(
            capture.payload(MessageType::DfsSyncDigestReply));
        TEST_REQUIRE(reply.has_value() && reply.value().mismatched.size() == 1);
        TEST_REQUIRE(reply.value().mismatched.front().owner_id == peer.id());
    }

    // 12. The gossiped removal (DfsFileRemove) carries the same signature as the
    //     tombstone; the receiver must apply it. The sender signs the cleared row
    //     (state Removed, empty hash/name/folder, size 0, new last_modified).
    {
        const std::string f4 = Utils::generate_random_hex(64);
        const auto        r4 = file_row(peer, f4, "d.txt", 1000);
        merge(peer.id(), { r4 });
        TEST_REQUIRE(row_of(peer.id(), f4).state != Dfs::FileState::Removed);
        // Signed by someone else: refused. (Was accepted: the check looked only at
        // whether verify() returned a value, never at the value.)
        const auto forged_t4 = tombstone(impostor, r4, 5000);
        node->dfs_service()->network_remove_stored_file(peer.id(), f4, forged_t4.sign, forged_t4.last_modified);
        TEST_REQUIRE(row_of(peer.id(), f4).state != Dfs::FileState::Removed);

        const auto t4 = tombstone(peer, r4, 6000);
        node->dfs_service()->network_remove_stored_file(peer.id(), f4, t4.sign, t4.last_modified);
        TEST_REQUIRE(row_of(peer.id(), f4).state == Dfs::FileState::Removed);
        TEST_REQUIRE(row_of(peer.id(), f4).sign == t4.sign);
    }

    {
        auto schema = Dfs::CollectionTemplate::create("catalog_root").value();
        schema.use_id().add_fields({ Dfs::Field::String("payload").not_null() });
        const auto stored = node->dfs()->store_template(owner.id(), schema).value();
        const auto file =
            node->dfs()->store_vector(owner.id(), owner.id(), "catalog_root", owner.id(), stored.file_id).value();
        auto       vector     = node->dfs()->make_vector(owner.id(), file.file_id).value().second;
        const auto old_digest = dirs.catalog_digests({ owner.id() }).front().digest;
        const auto old_hash   = row_of(owner.id(), file.file_id).hash;

        auto  package = vector.generate_content_package_empty().value();
        DbRow row { { "id", "snapshot-row" },
                    { "payload", "committed" },
                    { "actor", owner.id().to_string() },
                    { "status", "1" },
                    { "timestamp", std::to_string(Utils::current_date_ms()) } };
        row["sign"] = ByteArray(owner.key().sign(vector.calculate_hash(row).first).value()).toString();
        package.content.push_back(row);
        TEST_REQUIRE(vector.handle_package(package));
        const auto root = vector.index_root().value();
        TEST_REQUIRE(root.hash != old_hash);
        TEST_REQUIRE(row_of(owner.id(), file.file_id).hash == old_hash);
        const auto new_digest = dirs.catalog_digests({ owner.id() }).front().digest;
        TEST_REQUIRE_MESSAGE(new_digest != old_digest,
                             "a snapshot merge must change the digest even when the stored target is unchanged");

        const auto advertised = [&]() {
            Capture   capture;
            Responder target(&capture);
            target.add_identifier("peer-node");
            dirs.network_request_catalog_rows({ .owners = { owner.id() } }, target);
            capture.wait_messages(1);
            const auto rows =
                MessagePack::deserialize<Dfs::CatalogRowsPage>(capture.payload(MessageType::DfsSyncDirRows));
            TEST_REQUIRE(rows.has_value());
            for (const auto &candidate : rows.value().rows) {
                if (candidate.file_id == file.file_id)
                    return candidate;
            }
            TEST_REQUIRE_MESSAGE(false, "vector is missing from catalog response");
            return Dfs::DirRow { };
        };
        const auto response = advertised();
        TEST_REQUIRE(response.hash == root.hash && response.size == root.tree.bytes);
        TEST_REQUIRE(response.sign == file.sign && response.state == Dfs::FileState::Ready);
        TEST_REQUIRE(row_of(owner.id(), file.file_id).hash == old_hash);

        const auto path       = Dfs::Path::file_path(owner.id(), file.file_id).value().native();
        const auto saved_path = std::filesystem::path(path.string() + ".saved");
        std::filesystem::rename(path, saved_path);
        TEST_REQUIRE(dirs.catalog_digests({ owner.id() }).front().digest != new_digest);
        TEST_REQUIRE(advertised().state == Dfs::FileState::Known);
        std::filesystem::rename(saved_path, path);
        TEST_REQUIRE(dirs.catalog_digests({ owner.id() }).front().digest == new_digest);
    }

    {
        Actor<KeyPrivate> paged_owner;
        paged_owner.create(ActorType::User);
        const auto  count    = 2 * Dfs::CatalogPageRows + 3;
        auto        database = dirs.get_db_instance();
        std::string expected;
        TEST_REQUIRE(database->query("BEGIN IMMEDIATE"));
        for (std::size_t index = 0; index < count; ++index) {
            const auto row    = file_row(paged_owner, fmt::format("{:064x}", index), "paged", index + 1);
            const auto fields = Utils::to_dbrow(row);
            TEST_REQUIRE(database->insert("ActorsFiles", fields));
            expected += row.file_id;
            expected.push_back('\0');
            expected += fields.at("sign");
            expected.push_back('\0');
            expected += row.hash;
            expected.push_back('\n');
        }
        TEST_REQUIRE(database->query("COMMIT"));
        const auto digest = dirs.catalog_digests({ paged_owner.id() });
        TEST_REQUIRE_EQ(digest.size(), std::size_t(1));
        TEST_REQUIRE_EQ(digest.front().rows, count);
        TEST_REQUIRE_EQ(digest.front().digest, Utils::calculate_hash(expected));
        const auto all   = dirs.catalog_digests();
        const auto found = digest_of(all, paged_owner.id());
        TEST_REQUIRE(found != nullptr);
        TEST_REQUIRE_EQ(found->rows, count);
        TEST_REQUIRE_EQ(found->digest, digest.front().digest);
    }

    node->cleanUp();
    node.reset();
    std::error_code ignored;
    std::filesystem::current_path(std::filesystem::temp_directory_path(), ignored);
    std::filesystem::remove_all(test_path, ignored);
    std::printf("dfs catalog digest sync: PASS\n");
    return 0;
}
