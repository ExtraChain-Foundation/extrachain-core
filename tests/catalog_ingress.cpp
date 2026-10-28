#include <fstream>
#include <future>
#include <thread>

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "test_support.h"

using namespace std::chrono_literals;

int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-catalog-ingress-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner, stranger;
    owner.create(ActorType::User);
    stranger.create(ActorType::User);
    node->account_controller()->create_profile("catalog-ingress", ActorType::User, owner);
    auto      &dfs  = *node->dfs();
    auto       db   = dfs.get_db_instance();
    std::promise<void> executor_blocked, release_executor;
    auto               release = release_executor.get_future();
    boost::asio::post(node->serial_executor(), [&] {
        executor_blocked.set_value();
        release.wait();
    });
    executor_blocked.get_future().wait();
    std::atomic_uint delayed_callbacks { 0 };
    for (unsigned i = 0; i < 2; ++i) {
        dfs.schedule_delayed(0ms, [&] {
            ++delayed_callbacks;
        });
    }
    release_executor.set_value();
    const auto sign = [&](Dfs::DirRow &row) {
        row.sign = owner.key().sign(row.calculate_hash(owner.id())).value();
    };
    const auto make = [&](unsigned index) {
        Dfs::DirRow row;
        row.owner_id          = owner.id();
        row.actor_id          = owner.id();
        row.file_id           = Utils::calculate_hash("catalog-entry-" + std::to_string(index));
        row.prev_file_id      = std::string(64, 'a');
        row.name              = "folder";
        row.type              = Dfs::FileType::Folder;
        row.metadata_revision = 100;
        row.created           = 100;
        row.last_modified     = 100;
        sign(row);
        return row;
    };
    const auto wait = [&](auto condition) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!condition() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(5ms);
        TEST_REQUIRE(condition());
    };
    wait([&] {
        return delayed_callbacks.load() == 2;
    });
    DbConnector blocker(db->file());
    TEST_REQUIRE(blocker.open(false) && blocker.query("BEGIN IMMEDIATE"));
    std::atomic_uint accepted { 0 };
    for (unsigned peer = 0; peer < 4; ++peer) {
        for (unsigned index = 0; index < 8; ++index)
            TEST_REQUIRE(dfs.network_store_file(owner.id(),
                                                make(peer * 8 + index),
                                                Dfs::NetworkStoreFile::Sync,
                                                std::string(64, char('a' + peer)),
                                                [&] {
                                                    ++accepted;
                                                }));
        TEST_REQUIRE(!dfs.network_store_file(owner.id(),
                                             make(40),
                                             Dfs::NetworkStoreFile::Sync,
                                             std::string(64, char('a' + peer))));
    }
    TEST_REQUIRE_EQ(accepted.load(), 0u);
    TEST_REQUIRE(blocker.query("ROLLBACK"));
    wait([&] {
        return accepted.load() >= 32;
    });
    TEST_REQUIRE_EQ(accepted.load(), 32u);
    TEST_REQUIRE_EQ(db->count("ActorsFiles"), std::size_t(32));
    const auto submit = [&](const Dfs::DirRow &row) {
        TEST_REQUIRE(
            dfs.network_store_file(owner.id(), row, Dfs::NetworkStoreFile::Sync, std::string(64, 'a'), [&] {
                ++accepted;
            }));
    };
    auto forged = make(40);
    forged.name = "forged";
    submit(forged);
    TEST_REQUIRE(!dfs.network_store_file(stranger.id(), make(40), Dfs::NetworkStoreFile::Sync, "peer"));
    auto removed = Dfs::catalog_tombstone(owner.id(), make(41).file_id, 101, { });
    sign(removed);
    submit(removed);
    submit(removed);
    auto resurrected              = make(41);
    resurrected.metadata_revision = 999;
    sign(resurrected);
    submit(resurrected);
    submit(make(42));
    wait([&] {
        return accepted.load() >= 34;
    });
    TEST_REQUIRE_EQ(accepted.load(), 34u);
    TEST_REQUIRE(!Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db, owner.id(), make(40).file_id).has_value());
    TEST_REQUIRE(Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db, owner.id(), removed.file_id).value().state
                 == Dfs::FileState::Removed);

    const auto       file = make(0).file_id;
    auto             lock = dfs.download_manager().lock_file({ .owner_id = owner.id(), .file_id = file });
    std::atomic_bool removed_ok { false };
    std::jthread     removal([&] {
        removed_ok = dfs.remove_stored_file(owner.id(), file).has_value();
    });
    wait([&] {
        return Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db, owner.id(), file).value().state
               == Dfs::FileState::Removed;
    });
    const auto path = Dfs::Path::file_path(owner.id(), file).value();
    std::filesystem::create_directories(path.native().parent_path());
    {
        std::ofstream payload(path.native());
        payload << "late fragment";
    }
    lock.unlock();
    removal.join();
    TEST_REQUIRE(removed_ok.load());
    TEST_REQUIRE(!std::filesystem::exists(path.native()));
    std::atomic_uint completion_events { 0 };
    auto             completion = dfs.downloaded_event().subscribe([&](const auto &, const auto &) {
        ++completion_events;
    });
    dfs.download_manager().finish_him(owner.id(), make(0));
    TEST_REQUIRE_EQ(completion_events.load(), 0u);
    const auto companion = std::filesystem::path(path.native().string() + ".vector");
    {
        std::ofstream late(companion);
        late << "late companion";
    }
    const auto tombstone = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db, owner.id(), file).value();
    const auto cleanup   = dfs.accept_catalog_row(owner.id(), tombstone);
    TEST_REQUIRE(cleanup.has_value() && !cleanup.value().changed);
    TEST_REQUIRE(!std::filesystem::exists(companion));
    for (unsigned index = 50; index < 53; ++index) {
        auto large = make(index);
        large.type = Dfs::FileType::File;
        large.hash = std::string(64, 'b');
        large.size = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        sign(large);
        TEST_REQUIRE(dfs.accept_catalog_row(owner.id(), large).has_value());
    }
    dfs.refresh_calculate();
    TEST_REQUIRE_EQ(dfs.totalDfsSize(), std::numeric_limits<std::size_t>::max());
    dfs.prepare_shutdown();
    std::promise<void> late_callback;
    auto               late_result = late_callback.get_future();
    dfs.schedule_delayed(0ms, [&] {
        late_callback.set_value();
    });
    TEST_REQUIRE(late_result.wait_for(1s) == std::future_status::timeout);
    TEST_REQUIRE(!dfs.network_store_file(owner.id(), make(43), Dfs::NetworkStoreFile::Sync, "peer"));
    TEST_REQUIRE(blocker.close());
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
}
