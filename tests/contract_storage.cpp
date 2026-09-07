#include <filesystem>
#include <memory>

#include "chain/dag.h"
#include "contracts/dfs_contract_storage.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "test_support.h"
#include "../sources/contracts/contract_replay.h"

using namespace ExtraChain::Contracts;

int main() {
    ContractTransactionData graph;
    graph.transitions = { { .contract_id = "grandchild", .caller_contract_id = "child" },
                          { .contract_id = "sibling", .caller_contract_id = "root" },
                          { .contract_id = "child", .caller_contract_id = "root" } };
    TEST_REQUIRE_EQ(replay_call_depth(graph, "root", "root").value(), 0U);
    TEST_REQUIRE_EQ(replay_call_depth(graph, "root", "sibling").value(), 1U);
    TEST_REQUIRE_EQ(replay_call_depth(graph, "root", "grandchild").value(), 2U);
    TEST_REQUIRE(!replay_call_depth(graph, "root", "absent").has_value());
    graph.transitions.back().caller_contract_id = "grandchild";
    TEST_REQUIRE(!replay_call_depth(graph, "root", "grandchild").has_value());
    graph.transitions.clear();
    for (std::uint32_t depth = 1; depth <= ContractMaximumCallDepth + 1; ++depth) {
        graph.transitions.push_back(
            { .contract_id = std::to_string(depth), .caller_contract_id = std::to_string(depth - 1) });
    }
    TEST_REQUIRE_EQ(replay_call_depth(graph, "0", std::to_string(ContractMaximumCallDepth)).value(),
                    ContractMaximumCallDepth);
    TEST_REQUIRE(!replay_call_depth(graph, "0", std::to_string(ContractMaximumCallDepth + 1)).has_value());

    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-contract-storage-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->dag()->set_mode(DagMode::Light);
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("contract-storage", ActorType::User, owner);
    const auto contract = ActorId::create("1111111111111111111111111111111111111111");
    TEST_REQUIRE(contract.has_value());
    Transaction reward;
    reward.set_sender(owner.id());
    reward.set_receiver(owner.id());
    reward.set_token(owner.id());
    reward.set_type(TransactionType::Reward);
    reward.set_amount(BigNumberFloat("1"));
    reward.set_section(SectionId(1));
    reward.set_timestamp(1);
    TEST_REQUIRE(reward.sign(owner));
    TEST_REQUIRE(node->dag()->save_transaction(reward));
    node->dag()->set_current_section(SectionId(1));
    DfsContractStorage storage(node->dfs(), node->dag());
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto absent = storage.load(contract.value().to_string());
        TEST_REQUIRE(!absent.has_value());
        TEST_REQUIRE_EQ(absent.error().error, ContractError::NotFound);
    }
    const auto              revision = node->dag()->history_revision();
    ContractTransactionData deploy;
    deploy.kind        = "test";
    deploy.language    = "rust";
    deploy.module_hash = std::string(64, 'a');
    deploy.version     = 1;
    deploy.revision    = 1;
    deploy.checkpoint  = true;
    Transaction transaction;
    transaction.set_sender(owner.id());
    transaction.set_receiver(contract.value());
    transaction.set_type(TransactionType::ContractDeploy);
    transaction.set_section(SectionId(1));
    transaction.set_timestamp(2);
    transaction.set_meta(Json::serialize(deploy));
    TEST_REQUIRE(transaction.sign(owner));
    TEST_REQUIRE(node->dag()->save_transaction(transaction));
    TEST_REQUIRE(node->dag()->history_revision() != revision);
    TEST_REQUIRE_EQ(node->dag()->current_section(), SectionId(1));
    // A deployment in the same section invalidates the cached absence. Its
    // missing artifact is retryable; it must not be reported as a missing ID.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto unavailable = storage.load(contract.value().to_string());
        TEST_REQUIRE(!unavailable.has_value());
        TEST_REQUIRE_EQ(unavailable.error().error, ContractError::StorageError);
    }
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
