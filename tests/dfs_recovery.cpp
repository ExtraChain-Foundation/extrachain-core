#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/load_manager.h"
#include "managers/account_controller.h"
#include "network/responder.h"
#include "test_support.h"

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-dfs-recovery-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("dfs-recovery", ActorType::User, owner);
    node->dfs()->set_mode(DfsMode::Full);
    auto&      manager = node->dfs()->dirs_manager();
    const auto db      = manager.get_db_instance();
    const auto first   = ActorId::create(std::string(40, '1')).value();
    const auto second  = ActorId::create(std::string(40, '2')).value();
    using namespace Dfs::Tables::DirsFile;
    manager.update_dirs(first, 2000);
    manager.update_dirs(second, 1000);
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, first).value(), 2000U);
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, second).value(), 1000U);
    std::jthread newer([&] {
        for (unsigned i = 2000; i < 2100; ++i)
            manager.update_dirs(first, i);
    });
    std::jthread older([&] {
        for (unsigned i = 1000; i < 1100; ++i)
            manager.update_dirs(first, i);
    });
    newer.join();
    older.join();
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, first).value(), 2099U);
    manager.update_dirs(first, std::numeric_limits<std::uint64_t>::max());
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, first).value(), 2099U);

    // Reproduce persisted metadata after a crash, including a peer that knows
    // the file but cannot yet serve it. A size match alone cannot prove readiness.
    for (unsigned variant = 0; variant < 4; ++variant) {
        const auto  actor = ActorId::create(std::string(40, char('3' + variant))).value();
        Dfs::DirRow remote;
        remote.owner_id      = actor;
        remote.actor_id      = owner.id();
        remote.file_id       = std::string(64, char('a' + variant));
        remote.name          = "resume-" + std::to_string(variant);
        remote.size          = 1024;
        remote.last_modified = 500;
        remote.state         = variant >= 2 ? Dfs::FileState::Known : Dfs::FileState::Ready;
        remote.type          = Dfs::FileType::File;
        const auto path      = Dfs::Path::file_path(actor, remote.file_id);
        TEST_REQUIRE(path.has_value());
        std::filesystem::create_directories(
            std::filesystem::path(Dfs::Path::filePath(actor, remote.file_id)).parent_path());
        {
            std::ofstream file(Dfs::Path::filePath(actor, remote.file_id), std::ios::binary);
            file << std::string(1024, 'x');
        }
        remote.hash = Utils::calculate_hash_file(path.value()).value();
        auto local  = remote;
        local.state = variant == 1 ? Dfs::FileState::Ready : Dfs::FileState::Known;
        auto row    = Utils::to_dbrow(local);
        row.erase("prev_file_id");
        TEST_REQUIRE(db->insert(TableNameActorsFiles, row));
        {
            std::ofstream file(Dfs::Path::filePath(actor, remote.file_id), std::ios::binary);
            file << std::string(variant == 0 ? 512 : 1024, 'y');
        }
        TEST_REQUIRE_EQ(DirsSpace::last_modified(db, actor).value(), 0U);
        if (variant == 2) {
            std::filesystem::remove(Dfs::Path::filePath(actor, remote.file_id));
        }
        Responder responder;
        responder.add_identifier(std::string(64, 'c'));
        if (variant == 3) {
            manager.update_dirs(actor, remote.last_modified);
            node->dfs()->sync(std::string(64, 'c'));
        } else {
            manager.network_response_dir_rows({ { actor, { remote } } }, responder);
        }
        const Dfs::FileLink link { .owner_id = actor, .file_id = remote.file_id };
        const auto          deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool                queued   = false;
        while (!queued && std::chrono::steady_clock::now() < deadline) {
            queued = node->dfs()->download_manager().add_node_identifier(link, std::string(64, 'd'));
            if (!queued) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        TEST_REQUIRE(queued);
        TEST_REQUIRE_EQ(DirsSpace::last_modified(db, actor).value(), 500U);
        if (variant >= 2) {
            node->dfs()->network_response_file_state({ .owner_id = actor,
                                                       .file_id  = remote.file_id,
                                                       .state    = Dfs::FileState::Known },
                                                     responder);
            TEST_REQUIRE(node->dfs()->download_manager().add_node_identifier(link, std::string(64, 'e')));
        }
        const Dfs::Packets::FragmentData fragment { .owner_id              = actor,
                                                    .file_id               = remote.file_id,
                                                    .data                  = std::string(1024, 'x'),
                                                    .offset                = 0,
                                                    .current_size          = 1024,
                                                    .fragment_number       = 1,
                                                    .full_amount_fragments = 1 };
        node->dfs()->download_manager().file_fragment_achieved(fragment, std::string(64, 'd'));
        const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!node->dfs()->is_file_already_downloaded(actor, remote.file_id, remote.hash)
               && std::chrono::steady_clock::now() < recovery_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        TEST_REQUIRE(node->dfs()->is_file_already_downloaded(actor, remote.file_id, remote.hash));
        TEST_REQUIRE_EQ(path.value().file_size().value(), remote.size);
    }
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
