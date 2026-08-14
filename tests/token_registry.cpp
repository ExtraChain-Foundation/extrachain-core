#include <filesystem>
#include <memory>

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/token_manager.h"
#include "test_support.h"

int main() {
    const auto original_path = std::filesystem::current_path();
    const auto test_path     = std::filesystem::temp_directory_path() / "extrachain-token-registry-test";
    std::filesystem::remove_all(test_path);
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    TEST_REQUIRE(node->create_new_network("registry-login", "registry-password"));
    TEST_REQUIRE(node->create_token_template());

    const auto network_id = node->network_id();
    const auto registry_template =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          "TokensRegistry");
    TEST_REQUIRE(registry_template.has_value());
    const auto empty_registry = node->dfs()->store_vector(network_id,
                                                          network_id,
                                                          "TokensRegistry",
                                                          network_id,
                                                          registry_template->file_id);
    TEST_REQUIRE(empty_registry.has_value());

    // Resume after an interruption that left a ready vector without its native row.
    TEST_REQUIRE(node->create_token_vector());
    TEST_REQUIRE(node->create_token_template());
    TEST_REQUIRE(node->create_token_vector());

    const auto registry = node->dfs()->read_file_status(network_id, "TokensRegistry", Dfs::Basic::TEMPLATE_VECTOR);
    TEST_REQUIRE(registry.has_value());
    TEST_REQUIRE_EQ(registry->state, Dfs::FileState::Ready);

    const auto rows = node->dfs()->read_vector_rows(network_id, registry->file_id);
    TEST_REQUIRE(rows.has_value());
    TEST_REQUIRE_EQ(rows->size(), std::size_t(1));
    TEST_REQUIRE_EQ(rows->front().at("token_id"), TokenId().to_string());

    const auto native_token = node->token_manager()->token(TokenId());
    TEST_REQUIRE(native_token.has_value());
    TEST_REQUIRE_EQ(native_token->kind, std::string("native-token"));
    TEST_REQUIRE_EQ(native_token->ticker, std::string("EXC"));

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original_path);
    std::filesystem::remove_all(test_path);
    return 0;
}
