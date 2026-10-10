#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "dag_admission_fixture.h"
#include "managers/account_controller.h"
#include "managers/token_manager.h"
#include "test_support.h"
#include "utils/db_connector.h"

class TokenManagerTestAccess {
public:
    static std::set<std::string> planned(const TokenManager& manager) {
        std::set<std::string> hashes;
        for (const auto& record : manager.migration_plans())
            hashes.insert(record.transaction_hash);
        return hashes;
    }
};

// Migration plans are found through the index the cache pass keeps. Reading the whole
// history for them after every start held a restarted validator busy for over a minute.
int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-token-migrations-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    const auto start = [] {
        auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
        node->process();
        node->dag()->set_mode(DagMode::Full);
        return node;
    };
    auto node = start();
    node->account_controller()->create_profile("token-migrations", ActorType::User, owner);
    auto* dag = node->dag();

    Transaction initial;
    initial.set_type(TransactionType::Balance);
    initial.set_sender(owner.id());
    initial.set_receiver(owner.id());
    initial.set_token(owner.id());
    initial.set_section(SectionId(1));
    initial.set_amount(BigNumberFloat(5));
    TEST_REQUIRE(initial.sign(owner));
    TEST_REQUIRE(dag->save_transaction(initial));

    const auto migration = [&](int section) {
        LegacyTokenMigrationPlan plan;
        plan.legacy_token_id         = TokenId(owner.id().to_string());
        plan.target_contract_id      = owner.id();
        plan.owner_id                = owner.id();
        plan.source_section          = SectionId(1);
        plan.source_transaction_hash = initial.hash();
        plan.language                = "wasm";
        plan.module_hash             = std::string(64, 'a');
        plan.expected_supply         = "5";
        plan.cutoff_section          = SectionId(section + 100);
        plan.expires_section         = SectionId(section + 200);
        Transaction transaction;
        transaction.set_type(TransactionType::TokenMigration);
        transaction.set_sender(owner.id());
        transaction.set_receiver(owner.id());
        transaction.set_token(owner.id());
        transaction.set_section(SectionId(section));
        transaction.set_amount(BigNumberFloat(0));
        transaction.set_meta(Json::serialize(plan));
        TEST_REQUIRE(transaction.sign(owner));
        TEST_REQUIRE(DagAdmissionTestFixture::store_raw_section(
            *dag, Section { .id = SectionId(section), .transactions = { transaction } }));
        return transaction.hash();
    };
    const auto store_tip = [&](int section) {
        TEST_REQUIRE(DagAdmissionTestFixture::store_raw_section(*dag, Section { .id = SectionId(section) }));
        DagAdmissionTestFixture::start_chain_at_genesis(*dag);
        dag->set_current_section(SectionId(section));
        dag->update_range(true);
    };
    const auto planned = [&] {
        return TokenManagerTestAccess::planned(*node->token_manager());
    };

    const auto indexed = migration(10);
    store_tip(60);
    dag->set_current_section(SectionId(60));
    TEST_REQUIRE(dag->cache().check_and_update_cache(SectionId(60)).result);
    TEST_REQUIRE_EQ(dag->cache().section(), SectionId(40));
    // A plan past the cached section comes from the remaining sections.
    const auto later = migration(50);
    node->cleanUp();
    node.reset();
    node = start();
    dag  = node->dag();
    TEST_REQUIRE_EQ(dag->cache().section(), SectionId(40));

    // Without its section the history no longer holds the indexed plan; only the index does.
    TEST_REQUIRE(DagAdmissionTestFixture::erase_hot_section(*dag, SectionId(10)));
    TEST_REQUIRE(!dag->read_section(SectionId(10)).has_value());
    TEST_REQUIRE(planned() == (std::set<std::string> { indexed, later }));

    // An index that predates the cache-pass indexing is rebuilt once from the history.
    {
        DbConnector cache(ChainConst::BALANCE_CACHE);
        TEST_REQUIRE(cache.open());
        TEST_REQUIRE(cache.query("DELETE FROM token_migration_meta"));
    }
    node->cleanUp();
    node.reset();
    node = start();
    dag  = node->dag();
    TEST_REQUIRE(planned() == std::set<std::string> { later });

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
    std::puts("PASS");
}
