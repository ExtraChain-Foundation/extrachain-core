#include "dfs/vector_snapshot.h"
#include "test_support.h"

int main() {
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-snapshot-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    const auto  path = FsPath::create(directory / "vector.db").value();
    DbConnector writer(path);
    TEST_REQUIRE(writer.open());
    TEST_REQUIRE(writer.query("CREATE TABLE Vector(id TEXT PRIMARY KEY,payload TEXT NOT NULL)"));
    Dfs::VectorIndex index(writer, "id");
    TEST_REQUIRE(index.root().has_value());
    TEST_REQUIRE(writer.query("BEGIN IMMEDIATE"));
    constexpr unsigned Count = 2048;
    for (unsigned i = 0; i < Count; ++i) {
        const DbRow row { { "id", std::to_string(i) }, { "payload", std::string("old\0value", 9) } };
        TEST_REQUIRE(writer.replace("Vector", row));
        TEST_REQUIRE(index.update(row.at("id")).has_value());
    }
    TEST_REQUIRE(writer.query("COMMIT"));
    auto before = Dfs::VectorSnapshot::open(path, "id");
    TEST_REQUIRE(before.has_value());
    const auto old_root = before.value()->root();
    TEST_REQUIRE(Dfs::VectorIndex::valid_root(old_root));
    TEST_REQUIRE(writer.query("BEGIN IMMEDIATE"));
    TEST_REQUIRE(writer.replace("Vector", { { "id", "0" }, { "payload", "new value" } }));
    TEST_REQUIRE(index.update("0").has_value());
    TEST_REQUIRE(writer.query("COMMIT"));
    auto after = Dfs::VectorSnapshot::open(path, "id");
    TEST_REQUIRE(after.has_value());
    TEST_REQUIRE(after.value()->root().hash != old_root.hash);
    TEST_REQUIRE_EQ(before.value()->root().hash, old_root.hash);

    std::vector<Dfs::VectorIndexSummary> pending { old_root.tree };
    unsigned                             count = 0;
    while (!pending.empty()) {
        const auto expected = pending.back();
        pending.pop_back();
        const auto response = before.value()->read(expected.prefix);
        TEST_REQUIRE(response.has_value());
        const auto& slice = response.value();
        TEST_REQUIRE(Dfs::VectorSnapshot::verify(expected, "id", slice));
        for (const auto& row : slice.rows) {
            TEST_REQUIRE_EQ(row.at("payload"), std::string("old\0value", 9));
            ++count;
        }
        pending.insert(pending.end(), slice.children.begin(), slice.children.end());
    }
    TEST_REQUIRE_EQ(count, Count);
    const auto new_slice = after.value()->read(Dfs::VectorIndex::key_hash("0")).value();
    TEST_REQUIRE_EQ(new_slice.rows.size(), std::size_t(1));
    TEST_REQUIRE_EQ(new_slice.rows.front().at("payload"), std::string("new value"));
    TEST_REQUIRE(Dfs::VectorSnapshot::verify(new_slice.summary, "id", new_slice));
    auto bad_row                    = new_slice;
    bad_row.rows.front()["payload"] = "tampered";
    TEST_REQUIRE(!Dfs::VectorSnapshot::verify(new_slice.summary, "id", bad_row));
    bad_row = new_slice;
    bad_row.rows.push_back(bad_row.rows.front());
    TEST_REQUIRE(!Dfs::VectorSnapshot::verify(new_slice.summary, "id", bad_row));
    const auto old_branch = before.value()->read(old_root.tree.prefix).value();
    TEST_REQUIRE(!old_branch.children.empty());
    auto bad_branch                  = old_branch;
    bad_branch.children.front().rows = std::numeric_limits<std::uint64_t>::max();
    TEST_REQUIRE(!Dfs::VectorSnapshot::verify(old_root.tree, "id", bad_branch));
    bad_branch                         = old_branch;
    bad_branch.children.front().prefix = old_root.tree.prefix;
    TEST_REQUIRE(!Dfs::VectorSnapshot::verify(old_root.tree, "id", bad_branch));
    bad_branch                       = old_branch;
    bad_branch.children.front().hash = std::string(64, 'z');
    TEST_REQUIRE(!Dfs::VectorSnapshot::verify(old_root.tree, "id", bad_branch));
    bad_branch = old_branch;
    bad_branch.children.pop_back();
    TEST_REQUIRE(!Dfs::VectorSnapshot::verify(old_root.tree, "id", bad_branch));
    TEST_REQUIRE(!Dfs::VectorSnapshot::verify(after.value()->root().tree, "id", old_branch));
    auto bad_root = old_root;
    ++bad_root.version;
    TEST_REQUIRE(!Dfs::VectorIndex::valid_root(bad_root));
    TEST_REQUIRE(!before.value()->read("invalid").has_value());
    TEST_REQUIRE(before.value()->expired(std::chrono::steady_clock::now() + std::chrono::hours(1)));
    TEST_REQUIRE(!before.value()->expired(std::chrono::steady_clock::now()));
    before.value().reset();
    after.value().reset();
    TEST_REQUIRE(writer.close());
    std::filesystem::remove_all(directory);
    return 0;
}
