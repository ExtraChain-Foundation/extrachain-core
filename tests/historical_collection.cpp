#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "test_support.h"

#include <filesystem>
#include <memory>

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-history-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner, stranger;
    owner.create(ActorType::User);
    stranger.create(ActorType::User);
    node->account_controller()->create_profile("history", ActorType::User, owner);
    TEST_REQUIRE(node->actor_index()->save_actor(stranger.to_public()).has_value());
    auto schema = Dfs::CollectionTemplate::create("Ledger").value();
    schema.add_fields({ Dfs::Field::String("name").unique().not_null(),
                        Dfs::Field::Integer("count").default_value(7),
                        Dfs::Field::Real("value"),
                        Dfs::Field::Blob("blob") });
    auto reserved = Dfs::CollectionTemplate::create("HISTORICAL_CHAIN").value();
    reserved.add_fields({ Dfs::Field::String("value") });
    TEST_REQUIRE(!node->dfs()->store_collection(owner.id(), owner.id(), "Reserved", reserved).has_value());
    const auto file = node->dfs()->store_collection(owner.id(), owner.id(), "Ledger", schema);
    TEST_REQUIRE(file.has_value());
    auto chain = HistoricalCollection::load(node.get(), owner, owner.id(), file.value().file_id).value();
    TEST_REQUIRE_EQ(chain.get_historical_path().native(), chain.get_file_path().native());
    const auto added = node->dfs()->add_collection_row(owner.id(),
                                                       file.value().file_id,
                                                       { { "name", "one" },
                                                         { "value", "1.2300" },
                                                         { "blob", std::string("a\0b", 3) } });
    TEST_REQUIRE(added.has_value());
    TEST_REQUIRE(added.value().first.hash != file.value().hash);
    const auto one = added.value().second;
    TEST_REQUIRE_EQ(one.id, std::uint32_t(1));
    TEST_REQUIRE(HistoricalCollection::verify(node.get(), owner.id(), file.value().file_id, one));
    const auto read = node->dfs()->get_collection_row(owner.id(), file.value().file_id, one.id);
    TEST_REQUIRE(read.has_value());
    TEST_REQUIRE_EQ(read.value().at("count"), std::string("7"));
    TEST_REQUIRE_EQ(read.value().at("value"), std::string("1.23"));
    TEST_REQUIRE_EQ(read.value().at("blob"), std::string("a\0b", 3));
    auto root = [&] {
        return Dfs::Tables::DirsFile::ActorSpace::calculate_collection_hash_size(owner.id(), file.value().file_id);
    };
    auto initial = root();
    TEST_REQUIRE(!chain.add_row({ { "name", "one" } }, Dfs::DataSecurity::Public, { }).has_value());
    TEST_REQUIRE_EQ(root(), initial);
    TEST_REQUIRE_EQ(chain.get_last_row().value().id, one.id);
    TEST_REQUIRE(!chain.update_row(1234, { { "name", "missing" } }, Dfs::DataSecurity::Public, { }).has_value());
    TEST_REQUIRE_EQ(root(), initial);
    auto two = chain.add_row({ { "name", "two" } }, Dfs::DataSecurity::Public, { });
    TEST_REQUIRE(two.has_value());
    auto update =
        chain.update_row(one.id, { { "name", "changed" }, { "count", "9" } }, Dfs::DataSecurity::Public, { });
    TEST_REQUIRE(update.has_value());
    TEST_REQUIRE_EQ(node->dfs()->get_collection_row(owner.id(), file.value().file_id, one.id).value().at("count"),
                    std::string("9"));
    TEST_REQUIRE(
        !node->dfs()->get_collection_row(owner.id(), file.value().file_id, one.id).value().contains("blob"));
    auto delete_one = chain.remove_row(one.id);
    auto delete_two = chain.remove_row(two.value().id);
    TEST_REQUIRE(delete_one.has_value() && delete_two.has_value());
    TEST_REQUIRE(chain.get_collection_rows().value().empty());
    TEST_REQUIRE(!node->dfs()->get_collection_row(owner.id(), file.value().file_id, one.id).has_value());
    TEST_REQUIRE(!HistoricalCollection::load(node.get(), owner, owner.id(), "../escape").has_value());
    TEST_REQUIRE(!node->dfs()->get_collection_row(owner.id(), "../escape", 1).has_value());
    TEST_REQUIRE(!chain.get_row("0' OR 1=1").has_value());
    auto history = chain.get_historical_rows().value();
    TEST_REQUIRE_EQ(history.size(), std::size_t(6));
    initial        = root();
    auto duplicate = HistoricalCollection::accept(node.get(), owner.id(), file.value().file_id, history);
    TEST_REQUIRE(duplicate.has_value() && !duplicate.value());
    TEST_REQUIRE_EQ(root(), initial);
    auto bad     = one;
    bad.actor_id = stranger.id();
    bad.sign = stranger.key().sign(HistoricalCollection::row_hash(owner.id(), file.value().file_id, bad)).value();
    TEST_REQUIRE(!HistoricalCollection::accept(node.get(), owner.id(), file.value().file_id, { bad }).has_value());
    bad      = one;
    bad.data = "bad message pack";
    bad.sign = owner.key().sign(HistoricalCollection::row_hash(owner.id(), file.value().file_id, bad)).value();
    TEST_REQUIRE(!HistoricalCollection::accept(node.get(), owner.id(), file.value().file_id, { bad }).has_value());
    TEST_REQUIRE(!HistoricalCollection::accept(node.get(), owner.id(), file.value().file_id, { }).has_value());
    TEST_REQUIRE(!HistoricalCollection::verify(node.get(), owner.id(), Utils::generate_random_hex(64), one));
    const auto path = chain.get_file_path();
    std::filesystem::remove(path.native());
    TEST_REQUIRE(!HistoricalCollection::accept(node.get(), owner.id(), file.value().file_id, { bad }).has_value());
    TEST_REQUIRE(!std::filesystem::exists(path.native()));
    auto broken_page         = history;
    broken_page[2].prev_hash = std::string(64, '0');
    broken_page[2].sign =
        owner.key().sign(HistoricalCollection::row_hash(owner.id(), file.value().file_id, broken_page[2])).value();
    for (const auto& candidate : broken_page)
        TEST_REQUIRE(HistoricalCollection::verify(node.get(), owner.id(), file.value().file_id, candidate));
    TEST_REQUIRE(
        !HistoricalCollection::accept(node.get(), owner.id(), file.value().file_id, broken_page).has_value());
    if (std::filesystem::exists(path.native())) {
        DbConnector after_rollback(path);
        TEST_REQUIRE(after_rollback.open(false));
        TEST_REQUIRE(!after_rollback.table_exists("historical_chain"));
    }
    TEST_REQUIRE(HistoricalCollection::accept(node.get(), owner.id(), file.value().file_id, history).value());
    TEST_REQUIRE_EQ(root(), initial);
    auto replica = HistoricalCollection::load(node.get(), stranger, owner.id(), file.value().file_id).value();
    TEST_REQUIRE(replica.get_collection_rows().value().empty());
    TEST_REQUIRE(!replica.add_row({ { "name", "stranger" } }, Dfs::DataSecurity::Public, { }).has_value());
    TEST_REQUIRE_EQ(root(), initial);
    const auto paged = replica.get_historical_rows(2, 2).value();
    TEST_REQUIRE_EQ(paged.size(), std::size_t(2));
    TEST_REQUIRE_EQ(paged.front().id, std::uint32_t(2));
    TEST_REQUIRE(replica.get_historical_rows(6).value().empty());

    auto secret_schema = Dfs::CollectionTemplate::create("Secrets").value();
    secret_schema.add_fields({ Dfs::Field::String("text"), Dfs::Field::Integer("count").default_value(9) });
    Dfs::DataSecurityKey key;
    key.key.fill(0x41);
    auto encrypted = node->dfs()->store_collection(owner.id(),
                                                   owner.id(),
                                                   "Secrets",
                                                   secret_schema,
                                                   Dfs::DataSecurity::Key,
                                                   key);
    TEST_REQUIRE(encrypted.has_value());
    TEST_REQUIRE(!node->dfs()
                      ->add_collection_row(owner.id(), encrypted.value().file_id, { { "text", "secret" } })
                      .has_value());
    auto secured =
        node->dfs()->add_collection_row(owner.id(), encrypted.value().file_id, { { "text", "secret" } }, key);
    TEST_REQUIRE(secured.has_value());
    const auto plaintext =
        node->dfs()->get_collection_row(owner.id(), encrypted.value().file_id, secured.value().second.id, key);
    TEST_REQUIRE(plaintext.has_value());
    TEST_REQUIRE_EQ(plaintext.value().at("text"), std::string("secret"));
    TEST_REQUIRE_EQ(plaintext.value().at("count"), std::string("9"));
    TEST_REQUIRE(!node->dfs()
                      ->get_collection_row(owner.id(), encrypted.value().file_id, secured.value().second.id)
                      .has_value());
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
