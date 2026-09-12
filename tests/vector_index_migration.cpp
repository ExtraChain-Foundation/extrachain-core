#include <filesystem>
#include <memory>

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/dfs_vector.h"
#include "dfs/vector_index.h"
#include "managers/account_controller.h"
#include "test_support.h"

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-migrate-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("vector-migration", ActorType::User, owner);
    auto schema = Dfs::CollectionTemplate::create("migration_vector").value();
    schema.use_id().add_fields({ Dfs::Field::String("payload").not_null() });
    const auto stored = node->dfs()->store_template(owner.id(), schema);
    TEST_REQUIRE(stored.has_value());
    const auto file =
        node->dfs()->store_vector(owner.id(), owner.id(), "migration", owner.id(), stored.value().file_id);
    TEST_REQUIRE(file.has_value());
    const auto catalog      = node->dfs()->dirs_manager().get_db_instance();
    const auto catalog_hash = [&] {
        return Dfs::Tables::DirsFile::ActorSpace::get_dir_row(catalog, owner.id(), file.value().file_id)
            .value()
            .hash;
    };
    const auto set_hash = [&](const std::string& value) {
        TEST_REQUIRE(
            catalog->update(Dfs::Tables::DirsFile::TableNameActorsFiles,
                            { { "hash", value } },
                            { { "owner_id", owner.id().to_string() }, { "file_id", file.value().file_id } }));
    };
    const auto path         = Dfs::Path::file_path(owner.id(), file.value().file_id).value();
    const auto remove_index = [&] {
        DbConnector database(path);
        TEST_REQUIRE(database.open(false));
        for (const auto* statement : { "DROP TRIGGER IF EXISTS ExVectorInsert",
                                       "DROP TRIGGER IF EXISTS ExVectorUpdate",
                                       "DROP TRIGGER IF EXISTS ExVectorDelete",
                                       "DROP TABLE IF EXISTS ExVectorNodes",
                                       "DROP TABLE IF EXISTS ExVectorIndex" }) {
            TEST_REQUIRE(database.query(statement));
        }
        return database.hash_size("id").first;
    };
    auto       vector     = node->dfs()->make_vector(owner.id(), file.value().file_id).value().second;
    const auto empty_root = vector.data_hash_size().value().first;
    // The old empty-vector catalog stored the companion-file hash.
    set_hash(vector.calculate_template_file_hash().value().first);
    remove_index();
    TEST_REQUIRE_EQ(vector.data_hash_size().value().first, empty_root);
    TEST_REQUIRE_EQ(catalog_hash(), empty_root);
    TEST_REQUIRE(node->dfs()->add_vector_row(owner.id(),
                                             file.value().file_id,
                                             { { "id", "one" }, { "payload", "first" } }));
    TEST_REQUIRE(node->dfs()->add_vector_row(owner.id(),
                                             file.value().file_id,
                                             { { "id", "two" }, { "payload", "second" } }));
    const auto root   = vector.data_hash_size().value().first;
    const auto legacy = remove_index();
    TEST_REQUIRE(!legacy.empty() && legacy != root);
    set_hash(legacy);
    TEST_REQUIRE_EQ(vector.data_hash_size().value().first, root);
    TEST_REQUIRE_EQ(catalog_hash(), root);
    TEST_REQUIRE(node->dfs()->is_file_already_downloaded(owner.id(), file.value().file_id, root));
    TEST_REQUIRE_EQ(Dfs::Tables::DirsFile::ActorSpace::calculate_collection_hash_size(owner.id(),
                                                                                      file.value().file_id)
                        .first,
                    root);
    remove_index();
    const std::string expected_other_content(64, 'f');
    set_hash(expected_other_content);
    TEST_REQUIRE_EQ(vector.data_hash_size().value().first, root);
    TEST_REQUIRE_EQ(catalog_hash(), expected_other_content);
    // A cached legacy hash no longer describes the file after a direct table write.
    set_hash(legacy);
    {
        DbConnector database(path);
        TEST_REQUIRE(database.open(false));
        TEST_REQUIRE(database.query("UPDATE Vector SET payload='changed outside the index' WHERE id='one'"));
    }
    TEST_REQUIRE(vector.data_hash_size().value().first != root);
    TEST_REQUIRE_EQ(catalog_hash(), legacy);
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
