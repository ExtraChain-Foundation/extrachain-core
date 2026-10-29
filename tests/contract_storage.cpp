#include <filesystem>
#include <memory>

#include "chain/dag.h"
#include "chain/actor_index.h"
#include "contracts/dfs_contract_storage.h"
#include "contracts/contract_codec.h"
#include "contracts/standard_token.h"
#include "contracts/contract_hash.h"
#include "consensus/consensus_protocol.h"
#include "dfs/dfs_service.h"
#include "dfs/dfs_utils.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "test_support.h"
#include "utils/exc_utils_base64.h"
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
    const auto actor_count  = node->account_controller()->current_profile().actors().size();
    const auto token_module = standard_token_module(FungibleTokenKind, ToolchainLanguage::Rust);
    TEST_REQUIRE(token_module.has_value());
    const auto token_arguments = Codec::encode_json(R"(["Example Token","EXT",0,1000,[]])");
    TEST_REQUIRE(token_arguments.has_value());
    const auto protected_kind =
        node->submit_contract_deploy("fungible-token", token_module.value(), token_arguments.value());
    TEST_REQUIRE(!protected_kind.has_value());
    TEST_REQUIRE_EQ(protected_kind.error().error, ContractError::InvalidArguments);
    const auto custom_token =
        node->submit_contract_deploy("custom-token", token_module.value(), token_arguments.value());
    TEST_REQUIRE(!custom_token.has_value());
    TEST_REQUIRE_EQ(custom_token.error().error, ContractError::InvalidResponse);
    TEST_REQUIRE_EQ(custom_token.error().detail, "Contract initialization emitted an invalid effect");
    TEST_REQUIRE_EQ(node->account_controller()->current_profile().actors().size(), actor_count);
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
    {
        using namespace ExtraChain::Consensus;
        node->dag()->set_mode(DagMode::Full);
        Actor<KeyPrivate> deployed_actor;
        deployed_actor.create(ActorType::Service);
        node->account_controller()->create_service({}, deployed_actor);
        ContractManager temporary;
        const auto      change = temporary.prepare_deploy(deployed_actor.id().to_string(),
                                                     owner.id().to_string(),
                                                     std::string(FungibleTokenKind),
                                                     token_module.value(),
                                                     token_arguments.value(),
                                                     2);
        TEST_REQUIRE(change.has_value());
        const auto&             version = change.value().record.versions.back();
        const auto&             state   = version.revisions.back();
        ContractTransactionData metadata {
            .kind                = std::string(FungibleTokenKind),
            .language            = "rust",
            .method              = "init",
            .arguments_base64    = Utils::to_base64(token_arguments.value()),
            .module_hash         = version.module_hash,
            .state_hash          = state.state_hash,
            .effects_hash        = Codec::effect_hash(change.value().output.effects),
            .effects_base64      = Utils::to_base64(Codec::encode_effects(change.value().output.effects)),
            .checkpoint          = true,
            .checkpoint_revision = 1,
        };
        Transaction candidate;
        candidate.set_type(TransactionType::ContractDeploy);
        candidate.set_sender(owner.id());
        candidate.set_receiver(deployed_actor.id());
        candidate.set_amount(BigNumberFloat(0));
        candidate.set_section(SectionId(2));
        const auto batch_for = [&](const ContractTransactionData& data) {
            candidate.set_meta(Json::serialize(data));
            TEST_REQUIRE(candidate.sign(owner));
            WireFormat::Scope canonical(WireFormat::Mode::Canonical);
            SectionBatchData  batch;
            const auto bytes = Json::serialize(Section { .id = SectionId(2), .transactions = { candidate } });
            batch.sections.emplace_back(2, bytes);
            batch.manifest.first_section         = 2;
            batch.manifest.last_section          = 2;
            batch.manifest.transaction_hashes    = { candidate.hash() };
            batch.manifest.transaction_root      = calculate_transaction_root(batch.manifest.transaction_hashes);
            batch.manifest.data_root             = calculate_data_root(batch.sections);
            batch.manifest.previous_section_root = std::string(64, 'a');
            batch.manifest.payload_bytes         = bytes.size();
            return batch;
        };
        const auto validate = [&](SectionBatchData batch) {
            Proposal proposal;
            proposal.header.dag_section      = 2;
            proposal.header.section_root     = node->dag()->shadow_batch_section_root(batch).value();
            proposal.header.transaction_root = batch.manifest.transaction_root;
            proposal.header.batch_root       = hash_batch_manifest(batch.manifest);
            proposal.batch                   = batch.manifest;
            batch.header_hash                = hash_header(proposal.header);
            return node->dag()->validate_shadow_batch(proposal, batch, 16 * 1024 * 1024);
        };
        const auto pending        = batch_for(metadata);
        const auto missing_module = validate(pending);
        TEST_REQUIRE(!missing_module.has_value() && missing_module.error() == ConsensusError::DataUnavailable);
        TEST_REQUIRE(node->dag()->unprovable_batch_transactions(pending, {}).empty());
        TEST_REQUIRE(
            node->dfs()
                ->store_data_as_file(deployed_actor.id(),
                                     owner.id(),
                                     token_module.value(),
                                     Dfs::Basic::TEMPLATE_CONTRACTS,
                                     "contract-module-v000001-" + version.module_hash.substr(0, 12) + ".wasm")
                .has_value());
        const auto missing_checkpoint = validate(pending);
        TEST_REQUIRE(!missing_checkpoint.has_value()
                     && missing_checkpoint.error() == ConsensusError::DataUnavailable);
        TEST_REQUIRE(node->dfs()
                         ->store_data_as_file(deployed_actor.id(),
                                              owner.id(),
                                              state.state,
                                              Dfs::Basic::TEMPLATE_CONTRACTS,
                                              "contract-checkpoint-v000001-r000000000001-"
                                                  + state.state_hash.substr(0, 12) + ".msgpack")
                         .has_value());
        TEST_REQUIRE(validate(pending).has_value());
        metadata.effects_hash = std::string(64, '0');
        const auto invalid    = batch_for(metadata);
        const auto rejected   = validate(invalid);
        TEST_REQUIRE(!rejected.has_value() && rejected.error() == ConsensusError::InvalidRoot);
        TEST_REQUIRE_EQ(node->dag()->unprovable_batch_transactions(invalid, {}).size(), 1U);
    }
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
