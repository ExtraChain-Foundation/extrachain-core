#include <condition_variable>
#include <mutex>
#include <thread>

#include "chain/actor_index.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "network/responder.h"
#include "test_support.h"

using namespace std::chrono_literals;

struct Capture : ResponseSender {
    struct Message {
        MessageType   type;
        MessageStatus status;
        std::string   id;
        std::string   data;
    };
    mutable std::mutex      mutex;
    std::condition_variable ready;
    std::vector<Message>    messages;
    std::string             send_response(const std::string &data,
                                          MessageType        type,
                                          SendMode,
                                          MessageStatus    status,
                                          const Responder &target) override {
        std::lock_guard lock(mutex);
        messages.push_back({ type, status, target.message_id(), data });
        ready.notify_all();
        return target.message_id();
    }
    std::size_t size() const {
        std::lock_guard lock(mutex);
        return messages.size();
    }
    Message wait(std::size_t count) {
        std::unique_lock lock(mutex);
        TEST_REQUIRE(ready.wait_for(lock, 5s, [&] {
            return messages.size() >= count;
        }));
        return messages.at(count - 1);
    }
};

int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-catalog-protocol-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner, stranger;
    owner.create(ActorType::User);
    stranger.create(ActorType::User);
    node->account_controller()->create_profile("catalog-protocol", ActorType::User, owner);
    TEST_REQUIRE(node->actor_index()->save_actor(stranger.to_public()).has_value());
    auto      &dfs  = *node->dfs();
    auto      &dirs = dfs.dirs_manager();
    Capture    capture;
    const auto target = [&](std::string peer, std::string id = { }) {
        Responder result(&capture);
        result.add_identifier(peer);
        result.set_message_id(id);
        return result;
    };
    const auto wait = [&](auto condition) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!condition() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        TEST_REQUIRE(condition());
    };
    const auto make = [&](char index, const Actor<KeyPrivate> &author) {
        Dfs::DirRow row;
        row.owner_id          = author.id();
        row.actor_id          = author.id();
        row.file_id           = std::string(64, index);
        row.name              = "folder";
        row.created           = 100;
        row.metadata_revision = 100;
        row.last_modified     = 100;
        row.type              = Dfs::FileType::Folder;
        row.sign              = author.key().sign(row.calculate_hash(author.id())).value();
        return row;
    };
    const auto exists = [&](const Dfs::DirRow &row) {
        return Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs.get_db_instance(), row.owner_id, row.file_id)
            .has_value();
    };
    const auto send = [&](Dfs::CatalogRowsPage page, std::string id, std::string peer = "source") {
        dirs.network_response_dir_rows(MessagePack::serialize(page), target(peer, id));
    };
    const auto drain = [&] {
        const auto id = dirs.request_catalog_rows({ }, target("barrier"));
        TEST_REQUIRE(!id.empty());
        const auto before = dfs.staged_startup_response_count();
        send({ }, id, "barrier");
        wait([&] {
            return dfs.staged_startup_response_count() > before;
        });
    };
    const auto request = [&] {
        const auto id = dirs.request_catalog_rows({ .owners = { owner.id() } }, target("source"));
        TEST_REQUIRE(!id.empty());
        return id;
    };
    const auto first = make('1', owner);
    send({ .rows = { first } }, "000000000000001");
    drain();
    TEST_REQUIRE(!exists(first));
    auto id = request();
    send({ .rows = { first } }, "000000000000002");
    send({ .rows = { first } }, id, "other-peer");
    drain();
    TEST_REQUIRE(!exists(first));
    auto foreign = make('1', stranger);
    send({ .rows = { foreign } }, id);
    drain();
    TEST_REQUIRE(!exists(foreign));
    TEST_REQUIRE(!exists(first));
    id = request();
    dirs.network_response_dir_rows(std::string(Dfs::CatalogPageBytes + 1, 'x'), target("source", id));
    const auto second = make('2', owner);
    send({ .rows = { second, first } }, id);
    drain();
    TEST_REQUIRE(!exists(first) && !exists(second));
    id = request();
    send({ .rows = { first, first } }, id);
    drain();
    TEST_REQUIRE(!exists(first));
    id = request();
    send({ .rows = { first }, .next = Dfs::FileLink { .owner_id = owner.id(), .file_id = second.file_id } }, id);
    drain();
    TEST_REQUIRE(!exists(first));
    id = request();
    dirs.network_response_dir_rows(std::string(1, char(0xc1)), target("source", id));
    drain();
    TEST_REQUIRE(!exists(first));
    id            = request();
    auto bad_sign = first;
    bad_sign.name = "forged";
    send({ .rows = { bad_sign } }, id);
    drain();
    TEST_REQUIRE(!exists(first));

    id                = request();
    const auto before = capture.size();
    send({ .rows = { first, second },
           .next = Dfs::FileLink { .owner_id = owner.id(), .file_id = second.file_id } },
         id);
    const auto continuation = capture.wait(before + 1);
    TEST_REQUIRE(continuation.type == MessageType::DfsSyncDirRows
                 && continuation.status == MessageStatus::Request);
    TEST_REQUIRE(continuation.id != id);
    const auto next = MessagePack::deserialize<Dfs::CatalogRowsRequest>(continuation.data);
    TEST_REQUIRE(next.has_value() && next.value().after.file_id == second.file_id);
    TEST_REQUIRE(next.value().owners == std::vector<ActorId> { owner.id() });
    TEST_REQUIRE(exists(first) && exists(second));
    const auto third = make('3', owner);
    send({ .rows = { third } }, id);
    drain();
    TEST_REQUIRE(!exists(third));
    send({ .rows = { third } }, continuation.id);
    drain();
    TEST_REQUIRE(exists(third));
    auto forged_replay              = third;
    forged_replay.name              = "late replay";
    forged_replay.metadata_revision = 101;
    forged_replay.sign              = owner.key().sign(forged_replay.calculate_hash(owner.id())).value();
    send({ .rows = { forged_replay } }, continuation.id);
    drain();
    TEST_REQUIRE(Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs.get_db_instance(), owner.id(), third.file_id)
                     .value()
                     .name
                 == third.name);

    const auto digest_id = dirs.request_catalog_digest({ owner.id() }, target("digest-peer"));
    TEST_REQUIRE(!digest_id.empty());
    const auto reply = MessagePack::serialize(Dfs::Packets::CatalogDigestReply { });
    dirs.network_response_digest(reply, target("digest-peer", "000000000000003"));
    dirs.network_response_digest(reply, target("other-peer", digest_id));
    drain();
    TEST_REQUIRE(!dirs.digest_answered("digest-peer"));
    dirs.network_response_digest(reply, target("digest-peer", digest_id));
    drain();
    TEST_REQUIRE(dirs.digest_answered("digest-peer"));

    const auto prior_mode = dfs.mode();
    dfs.set_mode(DfsMode::Selective);
    const auto outside_selection =
        dirs.request_catalog_rows({ .owners = { stranger.id() } }, target("selection-peer"));
    TEST_REQUIRE(!outside_selection.empty());
    send({ .rows = { foreign } }, outside_selection, "selection-peer");
    drain();
    TEST_REQUIRE(!exists(foreign));
    dfs.set_mode(prior_mode);

    std::vector<std::string> pending;
    for (unsigned index = 0; index < 8; ++index) {
        const auto actor = ActorId::create(fmt::format("{:040x}", index + 10)).value();
        const auto entry = dirs.request_catalog_rows({ .owners = { actor } }, target("bounded-peer"));
        TEST_REQUIRE(!entry.empty());
        pending.push_back(entry);
    }
    TEST_REQUIRE(dirs.request_catalog_rows({ }, target("bounded-peer")).empty());
    for (const auto &entry : pending)
        send({ }, entry, "bounded-peer");
    drain();

    const auto expired = dirs.request_catalog_rows({ .owners = { owner.id() } }, target("expired-peer"));
    TEST_REQUIRE(!expired.empty());
    std::this_thread::sleep_for(30100ms);
    const auto fourth = make('4', owner);
    send({ .rows = { fourth } }, expired, "expired-peer");
    drain();
    TEST_REQUIRE(!exists(fourth));
    dfs.prepare_shutdown();
    TEST_REQUIRE(dirs.request_catalog_rows({ }, target("stopped")).empty());
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
}
