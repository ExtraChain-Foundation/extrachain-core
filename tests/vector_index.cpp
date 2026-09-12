#include <filesystem>
#include <set>

#include "dfs/vector_index.h"
#include "test_support.h"

int main() {
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-index-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    DbConnector first(directory / "first.db"), second(directory / "second.db");
    TEST_REQUIRE(first.open() && second.open());
    for (auto* db : { &first, &second }) {
        TEST_REQUIRE(
            db->query("CREATE TABLE Vector(id TEXT PRIMARY KEY,payload TEXT NOT NULL,status INTEGER NOT NULL)"));
    }
    Dfs::VectorIndex left(first, "id"), right(second, "id");
    const auto       initial = left.root();
    TEST_REQUIRE(initial.has_value());
    TEST_REQUIRE_EQ(initial.value().hash, right.root().value().hash);
    TEST_REQUIRE_EQ(initial.value().tree.rows, std::uint64_t(0));
    TEST_REQUIRE(!left.update("missing").has_value());
    constexpr unsigned Count = 1024;
    auto               row   = [](unsigned index) -> DbRow {
        return { { "id", "row-" + std::to_string(index) },
                 { "payload", std::string("value\0", 6) + std::to_string(index) },
                 { "status", "1" } };
    };
    TEST_REQUIRE(first.query("BEGIN IMMEDIATE"));
    TEST_REQUIRE(second.query("BEGIN IMMEDIATE"));
    for (unsigned i = 0; i < Count; ++i) {
        const auto ascending  = row(i);
        const auto descending = row(Count - i - 1);
        TEST_REQUIRE(first.replace("Vector", ascending));
        TEST_REQUIRE(left.update(ascending.at("id")).has_value());
        TEST_REQUIRE(second.replace("Vector", descending));
        TEST_REQUIRE(right.update(descending.at("id")).has_value());
    }
    TEST_REQUIRE(first.query("COMMIT") && second.query("COMMIT"));
    const auto populated = left.root().value();
    TEST_REQUIRE_EQ(populated.hash, right.root().value().hash);
    TEST_REQUIRE_EQ(populated.tree.rows, std::uint64_t(Count));
    TEST_REQUIRE(first.count("ExVectorNodes") < 2 * Count);
    TEST_REQUIRE(first.close() && first.open(false));
    TEST_REQUIRE_EQ(left.root().value().hash, populated.hash);

    TEST_REQUIRE(first.query("BEGIN IMMEDIATE"));
    auto changed       = row(10);
    changed["payload"] = "changed";
    TEST_REQUIRE(first.replace("Vector", changed));
    TEST_REQUIRE(left.update(changed.at("id")).has_value());
    TEST_REQUIRE(left.root().value().hash != populated.hash);
    TEST_REQUIRE(first.query("ROLLBACK"));
    TEST_REQUIRE_EQ(left.root().value().hash, populated.hash);

    std::set<std::string> found;
    std::string           after;
    for (;;) {
        const auto page = left.page({ }, after, 37);
        TEST_REQUIRE(page.has_value());
        if (page.value().empty())
            break;
        TEST_REQUIRE(page.value().size() <= 37);
        for (const auto& value : page.value()) {
            TEST_REQUIRE(found.insert(value.at("id")).second);
            const auto key = Dfs::VectorIndex::key_hash(value.at("id"));
            TEST_REQUIRE(key > after);
            after = key;
        }
    }
    TEST_REQUIRE_EQ(found.size(), std::size_t(Count));
    const auto children = left.children({ });
    TEST_REQUIRE(children.has_value() && children.value().size() <= 16);
    std::uint64_t total = 0;
    for (const auto& child : children.value())
        total += child.rows;
    TEST_REQUIRE_EQ(total, std::uint64_t(Count));
    const auto bucket = left.page("0", { }, 256);
    TEST_REQUIRE(bucket.has_value() && !bucket.value().empty());
    for (const auto& value : bucket.value())
        TEST_REQUIRE(Dfs::VectorIndex::key_hash(value.at("id")).starts_with("0"));
    TEST_REQUIRE(!left.page("z", { }, 1).has_value());
    TEST_REQUIRE(!left.page({ }, { }, 257).has_value());

    TEST_REQUIRE(first.query("UPDATE ExVectorNodes SET hash='damaged' WHERE prefix=''"));
    TEST_REQUIRE_EQ(left.root().value().hash, populated.hash);
    // Two writes before an index update must not lose the first changed leaf.
    TEST_REQUIRE(first.query("BEGIN IMMEDIATE"));
    auto tombstone      = row(8);
    tombstone["status"] = "0";
    TEST_REQUIRE(first.replace("Vector", tombstone));
    TEST_REQUIRE(first.replace("Vector", changed));
    TEST_REQUIRE(left.update(changed.at("id")).has_value());
    TEST_REQUIRE(first.query("COMMIT"));
    TEST_REQUIRE(second.replace("Vector", tombstone));
    TEST_REQUIRE(second.replace("Vector", changed));
    TEST_REQUIRE_EQ(left.root().value().hash, right.root().value().hash);
    TEST_REQUIRE(left.root().value().hash != populated.hash);
    TEST_REQUIRE(first.query("DELETE FROM Vector WHERE id='row-10'"));
    TEST_REQUIRE(second.query("DELETE FROM Vector WHERE id='row-10'"));
    TEST_REQUIRE_EQ(left.root().value().hash, right.root().value().hash);
    TEST_REQUIRE_EQ(left.root().value().tree.rows, std::uint64_t(Count - 1));

    TEST_REQUIRE(first.close() && second.close());
    std::filesystem::remove_all(directory);
    return 0;
}
