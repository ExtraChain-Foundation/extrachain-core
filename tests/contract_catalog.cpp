#include "chain/dag.h"
#include "contracts/contract_transaction.h"
#include "core/extrachain_node.h"
#include "dag_admission_fixture.h"
#include "managers/account_controller.h"
#include "test_support.h"
#include "utils/db_connector.h"

// The contract catalog follows the balance cache. Sections installed by sync are indexed by
// the cache pass, and a restart does not scan the history again (it used to, whenever the
// catalog was empty, i.e. on every node of a network without contracts).
int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-contract-catalog-" + Utils::generate_random_hex(8));
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
    node->account_controller()->create_profile("contract-catalog", ActorType::User, owner);
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

    const auto deploy = [&](int section, const std::string& kind) {
        Actor<KeyPrivate> contract;
        contract.create(ActorType::User);
        ContractTransactionData data;
        data.kind        = kind;
        data.language    = "wasm";
        data.module_hash = std::string(64, 'a');
        data.state_hash  = std::string(64, 'b');
        Transaction transaction;
        transaction.set_type(TransactionType::ContractDeploy);
        transaction.set_sender(owner.id());
        transaction.set_receiver(contract.id());
        transaction.set_token(owner.id());
        transaction.set_section(SectionId(section));
        transaction.set_amount(BigNumberFloat(0));
        transaction.set_meta(Json::serialize(data));
        TEST_REQUIRE(transaction.sign(owner));
        TEST_REQUIRE(DagAdmissionTestFixture::store_raw_section(
            *dag, Section { .id = SectionId(section), .transactions = { transaction } }));
        return contract.id().to_string();
    };
    // A restarted node takes its tip from stored sections; keep one at the tip so the cache,
    // which never sits past the tip, survives the restart as it does on a live node.
    const auto store_tip = [&](int section) {
        TEST_REQUIRE(DagAdmissionTestFixture::store_raw_section(*dag, Section { .id = SectionId(section) }));
        DagAdmissionTestFixture::start_chain_at_genesis(*dag);
        dag->set_current_section(SectionId(section));
        dag->update_range(true);
    };
    const auto listed = [&] {
        std::set<std::string> ids;
        for (const auto& item : dag->cache().list_contracts({ .limit = 100 }).items)
            ids.insert(item.contract_id);
        return ids;
    };
    const auto advance_cache = [&](int current) {
        dag->set_current_section(SectionId(current));
        TEST_REQUIRE(dag->cache().check_and_update_cache(SectionId(current)).result);
    };

    // The first cache pass covers a history without contracts: the catalog is empty but
    // complete, and a restart must not scan the history for it again.
    advance_cache(60);
    TEST_REQUIRE_EQ(dag->cache().section(), SectionId(40));
    const auto behind = deploy(10, "token");
    store_tip(60);
    node->cleanUp();
    node.reset();
    node = start();
    dag  = node->dag();
    TEST_REQUIRE_EQ(dag->cache().section(), SectionId(40));
    TEST_REQUIRE(listed().empty());

    // A section installed past the cache, as sync installs it, is indexed by the cache pass.
    const auto later = deploy(50, "vote");
    advance_cache(80);
    TEST_REQUIRE_EQ(dag->cache().section(), SectionId(60));
    TEST_REQUIRE(listed() == std::set<std::string> { later });
    store_tip(80);

    // A catalog that predates the cache-pass indexing is rebuilt once from the history.
    {
        DbConnector cache(ChainConst::BALANCE_CACHE);
        TEST_REQUIRE(cache.open());
        TEST_REQUIRE(cache.query("DELETE FROM contract_catalog_meta"));
    }
    node->cleanUp();
    node.reset();
    node = start();
    dag  = node->dag();
    dag->set_current_section(SectionId(80));
    TEST_REQUIRE(listed() == (std::set<std::string> { behind, later }));

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
    std::puts("PASS");
}
