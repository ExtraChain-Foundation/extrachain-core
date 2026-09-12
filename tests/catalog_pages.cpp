#include <filesystem>
#include <set>

#include "dfs/catalog_sync.h"
#include "test_support.h"

int main() {
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-catalog-pages-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    auto database = std::make_shared<DbConnector>(home / "catalog.sqlite");
    TEST_REQUIRE(database->open());
    TEST_REQUIRE(database->query(Dfs::Tables::DirsFile::CreateTableQueryActorsFiles));
    Actor<KeyPrivate> first, second;
    first.create(ActorType::User);
    second.create(ActorType::User);
    std::set<std::pair<ActorId, std::string>> expected;
    TEST_REQUIRE(database->query("BEGIN IMMEDIATE"));
    for (unsigned index = 0; index < 263; ++index) {
        const auto &owner = index < 259 ? first : second;
        Dfs::DirRow row;
        row.owner_id          = owner.id();
        row.actor_id          = owner.id();
        row.file_id           = Utils::calculate_hash("page-" + std::to_string(index));
        row.name              = "owner's entry";
        row.hash              = std::string(64, 'a');
        row.size              = 42;
        row.metadata_revision = index == 262 ? 0 : 100;
        row.sign              = owner.key().sign(row.calculate_hash(owner.id())).value();
        auto fields           = Utils::to_dbrow(row);
        fields.erase("prev_file_id");
        TEST_REQUIRE(database->insert("ActorsFiles", fields));
        if (row.metadata_revision != 0)
            expected.emplace(row.owner_id, row.file_id);
    }
    TEST_REQUIRE(database->query("COMMIT"));
    Dfs::CatalogRowsRequest                   request;
    std::set<std::pair<ActorId, std::string>> actual;
    unsigned                                  pages = 0;
    while (true) {
        const auto result = Dfs::read_catalog_page(database, request);
        TEST_REQUIRE(result.has_value());
        const auto &page = result.value();
        TEST_REQUIRE(Dfs::valid_catalog_page(page, request));
        TEST_REQUIRE(page.rows.size() <= Dfs::CatalogPageRows);
        for (const auto &row : page.rows)
            TEST_REQUIRE(actual.emplace(row.owner_id, row.file_id).second);
        ++pages;
        if (!page.next.has_value())
            break;
        request.after = page.next.value();
    }
    TEST_REQUIRE_EQ(pages, 3u);
    TEST_REQUIRE(actual == expected);
    const Dfs::CatalogRowsRequest filtered { .owners = { second.id() } };
    const auto                    subset = Dfs::read_catalog_page(database, filtered).value();
    TEST_REQUIRE_EQ(subset.rows.size(), std::size_t(3));
    TEST_REQUIRE(!subset.next.has_value());
    TEST_REQUIRE(Dfs::valid_catalog_page(subset, filtered));
    auto changed                  = subset;
    changed.rows.front().owner_id = first.id();
    TEST_REQUIRE(!Dfs::valid_catalog_page(changed, filtered));
    changed = subset;
    std::ranges::reverse(changed.rows);
    TEST_REQUIRE(!Dfs::valid_catalog_page(changed, filtered));
    changed      = subset;
    changed.next = Dfs::FileLink { .owner_id = second.id(), .file_id = std::string(64, 'f') };
    TEST_REQUIRE(!Dfs::valid_catalog_page(changed, filtered));
    changed                      = subset;
    changed.rows.front().file_id = "../escape";
    TEST_REQUIRE(!Dfs::valid_catalog_page(changed, filtered));
    auto bad_request = filtered;
    bad_request.owners.push_back(second.id());
    TEST_REQUIRE(!Dfs::read_catalog_page(database, bad_request).has_value());
    bad_request               = filtered;
    bad_request.after.file_id = "x' OR 1=1";
    TEST_REQUIRE(!Dfs::read_catalog_page(database, bad_request).has_value());
    bad_request = filtered;
    bad_request.owners.resize(Dfs::CatalogOwnerLimit + 1, first.id());
    TEST_REQUIRE(!Dfs::read_catalog_page(database, bad_request).has_value());
    TEST_REQUIRE(database->close());
    database.reset();
    std::filesystem::remove_all(home);
}
