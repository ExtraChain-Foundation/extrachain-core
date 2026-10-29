#include <filesystem>
#include <memory>

#include "contracts/toolchain_registry.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "test_support.h"
#include "utils/db_connector.h"
#include "utils/file_io.h"

int main() {
    using namespace ExtraChain::Contracts;
    const auto previous = std::filesystem::current_path();
    const auto root     = std::filesystem::temp_directory_path()
                      / ("extrachain-toolchain-registry-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(root);
    std::filesystem::current_path(root);
    TestSupport::Runner tests;
    tests.run("Signed toolchain metadata starts absent content downloads", [&] {
        auto settings       = Utils::read_settings();
        settings.first_node = "127.0.0.1";
        Utils::write_settings(settings);
        auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
        node->process();
        TEST_REQUIRE(node->create_new_network("toolchain-test", "toolchain-password"));
        const auto owner    = node->network_id();
        auto*      accounts = node->account_controller();
        TEST_REQUIRE(accounts->profile(accounts->current_profile().system().id()).change_current(owner));
        auto*                           registry = node->toolchain_registry();
        const std::vector<std::uint8_t> bytes { 1, 2, 3 };
        const auto                      package = registry->publish_package(ToolchainLanguage::AssemblyScript,
                                                       "linux",
                                                       "x86_64",
                                                       "tar.xz",
                                                       "0.3.0",
                                                       bytes);
        TEST_REQUIRE_MESSAGE(package.has_value(), package.has_value() ? "" : package.error().detail);
        ToolchainManifest manifest {
            .schema             = 3,
            .release_sequence   = 1,
            .language           = "assemblyscript",
            .version            = "0.3.0",
            .compiler_version   = "0.28.20",
            .runtime_version    = "24.10.0",
            .sdk_version        = "0.3.0",
            .components_version = "0.3.0",
            .catalog_version    = "0.3.0",
            .template_version   = "0.3.0",
            .contract_abi       = "4",
            .core_min           = "0.25.0",
            .core_max           = "0.26.0",
            .created            = 1,
            .packages           = { package.value() },
        };
        const auto published = registry->publish_manifest(manifest);
        TEST_REQUIRE_MESSAGE(published.has_value(), published.has_value() ? "" : published.error().detail);
        TEST_REQUIRE(registry->manifest(ToolchainLanguage::AssemblyScript).has_value());
        const auto row = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
            node->dfs()->get_db_instance(),
            owner,
            Dfs::Basic::TEMPLATE_CONTRACT_TOOLCHAIN,
            "contract-toolchain-assemblyscript-manifest-r00000000000000000001.json");
        TEST_REQUIRE(row.has_value());
        const auto metadata_path = root / "dfs" / owner.to_string() / row.value().file_id;
        const auto package_path  = root / "dfs" / owner.to_string() / package.value().file_id;
        std::filesystem::rename(metadata_path, root / "saved-manifest");
        std::filesystem::rename(package_path, root / "saved-package");
        const auto db = node->dfs()->get_db_instance();
        for (const auto state : { Dfs::FileState::Known, Dfs::FileState::Partial }) {
            Dfs::Tables::DirsFile::ActorSpace::update_file_state(db, owner, row.value().file_id, state);
            Dfs::Tables::DirsFile::ActorSpace::update_file_state(db, owner, package.value().file_id, state);
            const auto pending_manifest = registry->manifest(ToolchainLanguage::AssemblyScript);
            TEST_REQUIRE(!pending_manifest.has_value());
            TEST_REQUIRE_EQ(pending_manifest.error().error, ToolchainError::Unavailable);
            const auto pending_package = registry->package(package.value());
            TEST_REQUIRE(!pending_package.has_value());
            TEST_REQUIRE_EQ(pending_package.error().error, ToolchainError::Unavailable);
        }
        std::filesystem::rename(root / "saved-manifest", metadata_path);
        std::filesystem::rename(root / "saved-package", package_path);
        Dfs::Tables::DirsFile::ActorSpace::update_file_state(db,
                                                             owner,
                                                             row.value().file_id,
                                                             Dfs::FileState::Ready);
        Dfs::Tables::DirsFile::ActorSpace::update_file_state(db,
                                                             owner,
                                                             package.value().file_id,
                                                             Dfs::FileState::Ready);
        TEST_REQUIRE(registry->manifest(ToolchainLanguage::AssemblyScript).has_value());
        TEST_REQUIRE_EQ(registry->package(package.value()).value(), bytes);
        auto revoked             = manifest;
        revoked.release_sequence = 2;
        revoked.revoked_versions = { "0.3.0" };
        TEST_REQUIRE(registry->publish_manifest(revoked).has_value());
        const auto latest = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
            db,
            owner,
            Dfs::Basic::TEMPLATE_CONTRACT_TOOLCHAIN,
            "contract-toolchain-assemblyscript-manifest-r00000000000000000002.json");
        TEST_REQUIRE(latest.has_value());
        const auto latest_path = root / "dfs" / owner.to_string() / latest.value().file_id;
        std::filesystem::rename(latest_path, root / "saved-latest");
        for (const auto state : { Dfs::FileState::Known, Dfs::FileState::Partial }) {
            Dfs::Tables::DirsFile::ActorSpace::update_file_state(db, owner, latest.value().file_id, state);
            const auto pending_latest = registry->manifest(ToolchainLanguage::AssemblyScript);
            TEST_REQUIRE(!pending_latest.has_value());
            TEST_REQUIRE_EQ(pending_latest.error().error, ToolchainError::Unavailable);
        }
        Dfs::Tables::DirsFile::ActorSpace::update_file_state(db,
                                                             owner,
                                                             latest.value().file_id,
                                                             Dfs::FileState::Ready);
        TEST_REQUIRE(FileIo::write_atomic(latest_path, "invalid manifest bytes").has_value());
        const auto damaged_latest = registry->manifest(ToolchainLanguage::AssemblyScript);
        TEST_REQUIRE(!damaged_latest.has_value());
        TEST_REQUIRE_EQ(damaged_latest.error().error, ToolchainError::InvalidHash);
        TEST_REQUIRE(std::filesystem::remove(latest_path));
        std::filesystem::rename(root / "saved-latest", latest_path);
        const auto revoked_latest = registry->manifest(ToolchainLanguage::AssemblyScript);
        TEST_REQUIRE(!revoked_latest.has_value());
        TEST_REQUIRE_EQ(revoked_latest.error().error, ToolchainError::Revoked);
        TEST_REQUIRE(db->update("ActorsFiles",
                                { { "sign", Utils::to_base64(Signature {}) } },
                                { { "owner_id", owner.to_string() }, { "file_id", row.value().file_id } }));
        TEST_REQUIRE(!registry->manifest(ToolchainLanguage::AssemblyScript).has_value());
        node->cleanUp();
    });
    std::filesystem::current_path(previous);
    const auto result = tests.result();
    if (result == 0)
        std::filesystem::remove_all(root);
    return result;
}
