#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/vector_descriptor.h"
#include "managers/account_controller.h"
#include "test_support.h"

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-descriptor-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("vector-descriptor", ActorType::User, owner);
    auto schema = Dfs::CollectionTemplate::create("descriptor").value();
    schema.use_id().add_fields(
        { Dfs::Field::String("payload").not_null(), Dfs::Field::Integer("position").unique().not_null() });
    const auto stored = node->dfs()->store_template(owner.id(), schema);
    TEST_REQUIRE(stored.has_value());
    const auto vector_file =
        node->dfs()->store_vector(owner.id(), owner.id(), "descriptor", owner.id(), stored.value().file_id);
    TEST_REQUIRE(vector_file.has_value());
    const auto id     = vector_file.value().file_id;
    auto       vector = node->dfs()->make_vector(owner.id(), id).value().second;
    TEST_REQUIRE(node->dfs()->add_vector_row(owner.id(),
                                             id,
                                             { { "id", "one" }, { "payload", "first" }, { "position", "1" } }));
    auto       package   = vector.generate_content_package_empty().value();
    const auto path      = Dfs::Path::file_path(owner.id(), id).value();
    const auto companion = std::filesystem::path(path.native().string() + ".vector");
    std::filesystem::remove(companion);
    std::filesystem::create_directory(companion);
    std::filesystem::remove(Dfs::Path::file_path(owner.id(), stored.value().file_id).value().native());
    TEST_REQUIRE_EQ(Json::serialize(vector.read_template().value()), Json::serialize(schema));
    const auto signed_row = [&](const std::string& row_id, unsigned position) {
        DbRow      row { { "id", row_id },
                         { "payload", "value" },
                         { "position", std::to_string(position) },
                         { "status", "1" },
                         { "timestamp", std::to_string(Utils::current_date_ms()) },
                         { "actor", owner.id().to_string() } };
        const auto hash = vector.calculate_hash(row);
        TEST_REQUIRE(!hash.first.empty());
        const auto sign = owner.key().sign(hash.first);
        TEST_REQUIRE(sign.has_value());
        row["sign"] = ByteArray(sign.value()).toString();
        return row;
    };
    package.content = { signed_row("two", 2) };
    auto receiver   = node->dfs()->make_vector(owner.id(), id, true).value().second;
    TEST_REQUIRE(receiver.handle_package(package));
    TEST_REQUIRE_EQ(vector.index_root().value().tree.rows, std::uint64_t(2));
    TEST_REQUIRE(std::filesystem::is_directory(companion));
    const auto root       = vector.index_root().value().hash;
    const auto descriptor = [&] {
        DbConnector database(path);
        TEST_REQUIRE(database.open(false));
        return Json::serialize(Dfs::read_vector_descriptor(database).value().value());
    };
    const auto before = descriptor();
    package.content   = { signed_row("three", 3), signed_row("four", 3) };
    TEST_REQUIRE(!receiver.handle_package(package));
    TEST_REQUIRE_EQ(vector.index_root().value().hash, root);
    TEST_REQUIRE_EQ(descriptor(), before);
    TEST_REQUIRE(!node->dfs()->add_vector_row(owner.id(),
                                              id,
                                              { { "id", "collision" },
                                                { "payload", "replacement" },
                                                { "position", "1" } }));
    TEST_REQUIRE_EQ(vector.index_root().value().hash, root);
    package.content.clear();
    auto changed = package;
    changed.vector_template.add_fields({ Dfs::Field::String("foreign") });
    TEST_REQUIRE(!receiver.handle_package(changed));
    changed             = package;
    changed.vector_file = "invalid JSON";
    TEST_REQUIRE(!receiver.handle_package(changed));
    TEST_REQUIRE_EQ(descriptor(), before);
    TEST_REQUIRE_EQ(vector.index_root().value().hash, root);
    std::filesystem::remove(companion);
    TEST_REQUIRE(receiver.handle_package(package));
    TEST_REQUIRE(std::filesystem::is_regular_file(companion));
    TEST_REQUIRE_EQ(vector.read_rows().value().size(), std::size_t(2));
    TEST_REQUIRE(!DfsVector::load_network(node.get(), owner, owner.id(), "../escape").has_value());

    DbConnector probe(directory / "probe.db");
    TEST_REQUIRE(probe.open());
    auto storage = Dfs::vector_storage_template(schema, false).value().to_db_schema().value();
    storage.set_table_name("Vector");
    TEST_REQUIRE(probe.create_table(storage).has_value());
    const Dfs::VectorDescriptor pending { .schema = schema, .companion = Json::serialize(schema) };
    TEST_REQUIRE(!Dfs::store_vector_descriptor(probe, pending, false).has_value());
    TEST_REQUIRE(probe.query("BEGIN IMMEDIATE"));
    TEST_REQUIRE(Dfs::store_vector_descriptor(probe, pending, false).has_value());
    TEST_REQUIRE(probe.table_exists("ExVectorDescriptor"));
    TEST_REQUIRE(probe.query("ROLLBACK"));
    TEST_REQUIRE(!probe.table_exists("ExVectorDescriptor") && !probe.table_exists("ExVectorIndex"));
    TEST_REQUIRE(probe.close());
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
