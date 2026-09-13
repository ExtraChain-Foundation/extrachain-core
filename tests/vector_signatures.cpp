#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "test_support.h"

#include <charconv>
#include <filesystem>
#include <limits>
#include <memory>
#include <sqlite3.h>

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-signatures-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("vector-signatures", ActorType::User, owner);
    auto schema = Dfs::CollectionTemplate::create("SignedRows").value();
    schema.use_id().add_fields({ Dfs::Field::String("left_value"), Dfs::Field::String("right_value") });
    const auto stored = node->dfs()->store_template(owner.id(), schema);
    TEST_REQUIRE(stored.has_value());
    const auto file =
        node->dfs()->store_vector(owner.id(), owner.id(), "SignedRows", owner.id(), stored.value().file_id);
    TEST_REQUIRE(file.has_value());
    auto  vector = node->dfs()->make_vector(owner.id(), file.value().file_id).value().second;
    DbRow row { { "id", "one" }, { "left_value", "ab" }, { "right_value", "c" } };
    TEST_REQUIRE(vector.store_add(row));
    TEST_REQUIRE(vector.verify(row));
    const auto root        = vector.index_root().value().hash;
    auto       altered     = row;
    altered["left_value"]  = "a";
    altered["right_value"] = "bc";
    TEST_REQUIRE(!vector.verify(altered));
    TEST_REQUIRE(!vector.local_add(altered, false).has_value());
    auto package    = vector.generate_content_package_empty().value();
    package.content = { altered };
    TEST_REQUIRE(!vector.handle_package(package));
    TEST_REQUIRE_EQ(vector.index_root().value().hash, root);
    altered            = row;
    altered["unknown"] = "payload";
    TEST_REQUIRE(!vector.verify(altered));
    altered           = row;
    altered["status"] = "2";
    TEST_REQUIRE(!vector.verify(altered));
    altered              = row;
    altered["timestamp"] = "0" + row.at("timestamp");
    TEST_REQUIRE(vector.calculate_hash(altered).first.empty());
    altered["timestamp"] = "9223372036854775808";
    TEST_REQUIRE(vector.calculate_hash(altered).first.empty());
    altered = row;
    Actor<KeyPrivate> stranger;
    stranger.create(ActorType::User);
    altered["actor"] = stranger.id().to_string();
    TEST_REQUIRE(vector.calculate_hash(altered).first != vector.calculate_hash(row).first);
    altered               = row;
    altered["left_value"] = "";
    const auto empty_hash = vector.calculate_hash(altered).first;
    altered.erase("left_value");
    TEST_REQUIRE(vector.calculate_hash(altered).first != empty_hash);
    DbRow replacement { { "id", "one" }, { "right_value", "new" } };
    TEST_REQUIRE(vector.store_add(replacement));
    TEST_REQUIRE(!replacement.contains("left_value"));
    TEST_REQUIRE(std::stoull(replacement.at("timestamp")) > std::stoull(row.at("timestamp")));
    TEST_REQUIRE(vector.verify(replacement));
    auto replay = vector.local_add(row, true);
    TEST_REQUIRE(replay.has_value() && !replay.value());
    DbRow empty { { "id", "empty" }, { "left_value", "" }, { "right_value", "" } };
    TEST_REQUIRE(vector.store_add(empty));
    TEST_REQUIRE(vector.verify(empty));

    auto numbers = Dfs::CollectionTemplate::create("Numbers").value();
    numbers.use_id().add_fields({ Dfs::Field::Real("value").not_null(),
                                  Dfs::Field::Integer("count").default_value(7),
                                  Dfs::Field::Blob("optional_value") });
    const auto number_template = node->dfs()->store_template(owner.id(), numbers);
    TEST_REQUIRE(number_template.has_value());
    const auto number_file =
        node->dfs()->store_vector(owner.id(), owner.id(), "Numbers", owner.id(), number_template.value().file_id);
    TEST_REQUIRE(number_file.has_value());
    auto number_vector = node->dfs()->make_vector(owner.id(), number_file.value().file_id).value().second;
    for (const auto value : { 1.2345678901234567,
                              1e-250,
                              1e250,
                              std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::denorm_min() }) {
        std::array<char, 64> buffer;
        const auto           encoded = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        TEST_REQUIRE(encoded.ec == std::errc());
        DbRow number { { "id", Utils::generate_random_hex(8) },
                       { "value", std::string(buffer.data(), encoded.ptr) } };
        TEST_REQUIRE(number_vector.store_add(number));
        TEST_REQUIRE(number_vector.verify(number));
        TEST_REQUIRE_EQ(number.at("count"), std::string("7"));
        TEST_REQUIRE(!number.contains("optional_value"));
        double      decoded = 0;
        const auto& text    = number.at("value");
        const auto  parsed  = std::from_chars(text.data(), text.data() + text.size(), decoded);
        TEST_REQUIRE(parsed.ec == std::errc() && parsed.ptr == text.data() + text.size());
        TEST_REQUIRE_EQ(decoded, value);
        const auto loaded = number_vector.read_row(number.at("id"));
        TEST_REQUIRE(loaded.has_value() && loaded.value() == number);
    }
    const auto number_root    = number_vector.index_root().value().hash;
    auto       number_package = number_vector.generate_content_package().value();
    const auto number_path    = Dfs::Path::file_path(owner.id(), number_file.value().file_id).value();
    {
        DbConnector database(number_path);
        TEST_REQUIRE(database.open(false));
        Dfs::VectorIndex merkle(database, "id");
        const auto       page = merkle.page("", "", 256);
        TEST_REQUIRE(page.has_value() && page.value().size() == 5);
        for (const auto& value : page.value())
            TEST_REQUIRE(number_vector.verify(value));
        TEST_REQUIRE(database.query("UPDATE ExVectorIndex SET schema_hash='old-format'"));
        TEST_REQUIRE_EQ(merkle.root().value().hash, number_root);
        auto        cursor = database.select_while("SELECT * FROM Vector ORDER BY id", "Vector");
        const auto  rows   = database.select("SELECT * FROM Vector ORDER BY id");
        std::size_t index  = 0;
        while (cursor->next()) {
            TEST_REQUIRE(index < rows.size());
            TEST_REQUIRE(cursor->dbRow() == rows[index++]);
        }
        TEST_REQUIRE(!cursor->failed() && index == rows.size());
        cursor.reset();
        auto failed = database.select_while("SELECT abs(-9223372036854775808)", "Vector");
        TEST_REQUIRE(failed != nullptr);
        TEST_REQUIRE(!failed->next() && failed->failed());
        TEST_REQUIRE(!failed->next());
    }
    std::filesystem::remove(number_path.native());
    TEST_REQUIRE(number_vector.handle_package(number_package));
    TEST_REQUIRE_EQ(number_vector.index_root().value().hash, number_root);
    const Dfs::DataSecurityKey key { .key = Cryptography::keygen() };
    const auto                 encrypted_file = node->dfs()->store_vector(owner.id(),
                                                                          owner.id(),
                                                                          "Encrypted",
                                                                          owner.id(),
                                                                          stored.value().file_id,
                                                                          Dfs::DataSecurity::Key,
                                                                          key);
    TEST_REQUIRE(encrypted_file.has_value());
    auto  encrypted = node->dfs()
                          ->make_vector(owner.id(), encrypted_file.value().file_id, false, owner.id(), key)
                          .value()
                          .second;
    DbRow secret { { "id", "secret" }, { "left_value", "private" }, { "right_value", "message" } };
    TEST_REQUIRE(encrypted.store_add(secret));
    TEST_REQUIRE(secret.at("left_value") != "private");
    const auto removed = encrypted.remove("secret");
    TEST_REQUIRE(removed.has_value() && removed.value().at("status") == "0");
    TEST_REQUIRE(encrypted.verify(removed.value()));
    TEST_REQUIRE(!encrypted.read_row("secret").has_value());
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
