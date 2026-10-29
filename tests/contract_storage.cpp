#include <filesystem>
#include <memory>

#include <fmt/format.h>

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
    {
        using namespace ExtraChain::Consensus;
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
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
        ContractTransactionData input {
            .kind             = std::string(FungibleTokenKind),
            .language         = "rust",
            .method           = "init",
            .arguments_base64 = Utils::to_base64(token_arguments.value()),
            .module_hash      = version.module_hash,
        };
        const auto request_metadata = Json::serialize(input);
        const auto intent           = make_intent(
            TransactionIntentV2 {
                          .network_id           = owner.id(),
                          .sender               = owner.id(),
                          .receiver             = deployed_actor.id(),
                          .amount               = "0",
                          .operation            = IntentOperation::ContractDeploy,
                          .account_nonce        = 1,
                          .valid_after_height   = 1,
                          .expires_after_height = 20,
            },
            request_metadata,
            owner);
        TEST_REQUIRE(intent.has_value());
        const IntentEnvelope request { .intent = intent.value(), .metadata = request_metadata };
        auto                 pending = materialize_intent(request, 2, 1, {});
        TEST_REQUIRE(pending.has_value());
        TEST_REQUIRE_EQ(node->complete_contract_intent(pending.value()),
                        TransactionProveError::ContractDependencyMissing);
        Actor<KeyPrivate> other;
        other.create(ActorType::User);
        const auto transfer = [&](const Actor<KeyPrivate>& sender, std::uint64_t nonce) {
            const auto signed_request = make_intent(
                TransactionIntentV2 {
                    .network_id           = owner.id(),
                    .sender               = sender.id(),
                    .receiver             = deployed_actor.id(),
                    .amount               = "1",
                    .operation            = IntentOperation::Transfer,
                    .account_nonce        = nonce,
                    .valid_after_height   = 1,
                    .expires_after_height = 20,
                },
                {},
                sender);
            TEST_REQUIRE(signed_request.has_value());
            return IntentEnvelope { .intent = signed_request.value() };
        };
        std::vector<std::string> rejected;
        std::vector<std::string> deferred;
        const auto               batch =
            node->dag()->build_shadow_intent_batch(SectionId(2),
                                                   SectionId(2),
                                                   1,
                                                   { request, transfer(owner, 2), transfer(other, 1) },
                                                   16 * 1024 * 1024,
                                                   {},
                                                   Json::serialize(Section { .id = SectionId(1) }),
                                                   std::string(64, 'a'),
                                                   std::nullopt,
                                                   &rejected,
                                                   &deferred);
        TEST_REQUIRE(batch.has_value() && rejected.empty());
        TEST_REQUIRE_EQ(deferred, std::vector<std::string> { hash_intent(request.intent) });
        TEST_REQUIRE_EQ(batch.value().manifest.transaction_hashes.size(), 1U);
        const auto section = Json::deserialize<Section>(batch.value().sections.front().second);
        TEST_REQUIRE(section.has_value() && section.value().transactions.size() == 1);
        TEST_REQUIRE_EQ(section.value().transactions.begin()->sender(), other.id());
        TEST_REQUIRE_EQ(section.value().transactions.begin()->section(), SectionId(2));
        TEST_REQUIRE(storage.stage(change.value().record, false).has_value());
        TEST_REQUIRE_EQ(node->complete_contract_intent(pending.value()), TransactionProveError::NoError);
        TEST_REQUIRE(pending.value().verify(owner.to_public()));
        TEST_REQUIRE_EQ(pending.value().request_hash(), hash_intent(intent.value()));
        const auto output = Json::deserialize<ContractTransactionData>(pending.value().meta().value());
        TEST_REQUIRE(output.has_value() && output.value().checkpoint_state_base64.has_value());
        TEST_REQUIRE_EQ(Utils::from_base64<std::vector<std::uint8_t>>(
                            output.value().checkpoint_state_base64.value())
                            .value(),
                        change.value().output.state);
        const auto checkpoint_name =
            "contract-checkpoint-v000001-r000000000001-" + output.value().state_hash.substr(0, 12) + ".msgpack";
        TEST_REQUIRE(!node->dfs()
                          ->read_file_status(deployed_actor.id(), checkpoint_name, Dfs::Basic::TEMPLATE_CONTRACTS)
                          .has_value());
        auto forged              = pending.value();
        auto forged_output       = output.value();
        forged_output.state_hash = std::string(64, '0');
        forged.set_meta(Json::serialize(forged_output));
        forged.update_hash();
        TEST_REQUIRE(forged.verify(owner.to_public()));
        TEST_REQUIRE_EQ(node->validate_contract_transaction(forged, false),
                        TransactionProveError::InvalidContractPayload);
        TEST_REQUIRE_EQ(node->validate_contract_transaction(pending.value()), TransactionProveError::NoError);
        TEST_REQUIRE(node->dag()->save_transaction(pending.value()));
        node->dag()->set_current_section(SectionId(2));
        node->finalize_contract_change(pending.value().hash(), true);
        const auto committed = node->contract_manager()->inspect(deployed_actor.id().to_string());
        TEST_REQUIRE(committed.has_value());
        TEST_REQUIRE_EQ(committed.value().versions.back().revisions.back().block, 2U);
        TEST_REQUIRE(committed.value().versions.back().revisions.back().checkpoint_storage_id.empty());
        TEST_REQUIRE(!node->dfs()
                          ->read_file_status(deployed_actor.id(), checkpoint_name, Dfs::Basic::TEMPLATE_CONTRACTS)
                          .has_value());
        std::filesystem::remove(std::filesystem::path("contract-heads")
                                / (deployed_actor.id().to_string() + ".msgpack"));
        DfsContractStorage recovered(node->dfs(), node->dag());
        const auto         replayed = recovered.load(deployed_actor.id().to_string());
        TEST_REQUIRE(replayed.has_value());
        TEST_REQUIRE_EQ(replayed.value().versions.back().revisions.back().state, change.value().output.state);
        TEST_REQUIRE_EQ(replayed.value().versions.back().revisions.back().transaction_hash,
                        pending.value().hash());
        TEST_REQUIRE(node->actor_index()->store_new_actor(other.to_public()).has_value());
        std::uint64_t             owner_nonce = 1;
        std::uint64_t             other_nonce = 0;
        std::vector<std::uint8_t> checkpoint_state;
        constexpr auto            boundary_revision = ContractCheckpointInterval + 1;
        for (std::uint64_t next_revision = 2; next_revision <= boundary_revision; ++next_revision) {
            const bool  from_owner = next_revision % 2 == 0;
            const auto& caller     = from_owner ? owner : other;
            const auto  recipient  = (from_owner ? other.id() : owner.id()).to_string();
            const auto  arguments  = Codec::encode_json("[\"" + recipient + "\",1]");
            TEST_REQUIRE(arguments.has_value());
            const auto prior = node->contract_manager()->inspect(deployed_actor.id().to_string());
            TEST_REQUIRE(prior.has_value());
            const auto&                   prior_version  = prior.value().versions.back();
            const auto&                   prior_revision = prior_version.revisions.back();
            const ContractTransactionData call_input {
                .kind                = std::string(FungibleTokenKind),
                .language            = "rust",
                .method              = "transfer",
                .arguments_base64    = Utils::to_base64(arguments.value()),
                .module_hash         = prior_version.module_hash,
                .previous_state_hash = prior_revision.state_hash,
                .revision            = next_revision,
            };
            const auto metadata    = Json::serialize(call_input);
            const auto signed_call = make_intent(
                TransactionIntentV2 {
                    .network_id           = owner.id(),
                    .sender               = caller.id(),
                    .receiver             = deployed_actor.id(),
                    .amount               = "0",
                    .operation            = IntentOperation::ContractCall,
                    .account_nonce        = from_owner ? ++owner_nonce : ++other_nonce,
                    .valid_after_height   = 1,
                    .expires_after_height = 1000,
                },
                metadata,
                caller);
            TEST_REQUIRE(signed_call.has_value());
            auto call = materialize_intent(IntentEnvelope { .intent = signed_call.value(), .metadata = metadata },
                                           next_revision + 1,
                                           1,
                                           {});
            TEST_REQUIRE(call.has_value());
            TEST_REQUIRE_EQ(node->complete_contract_intent(call.value()), TransactionProveError::NoError);
            TEST_REQUIRE_EQ(node->validate_contract_transaction(call.value()), TransactionProveError::NoError);
            const auto result = Json::deserialize<ContractTransactionData>(call.value().meta().value());
            TEST_REQUIRE(result.has_value());
            TEST_REQUIRE_EQ(result.value().checkpoint, next_revision == boundary_revision);
            TEST_REQUIRE(node->dag()->save_transaction(call.value()));
            node->dag()->set_current_section(SectionId(next_revision + 1));
            node->finalize_contract_change(call.value().hash(), true);
            if (next_revision == boundary_revision) {
                TEST_REQUIRE_EQ(caller.id(), other.id());
                TEST_REQUIRE(result.value().checkpoint_state_base64.has_value());
                checkpoint_state =
                    Utils::from_base64<std::vector<std::uint8_t>>(result.value().checkpoint_state_base64.value())
                        .value();
                const auto name = fmt::format("contract-checkpoint-v000001-r{:012}-{}.msgpack",
                                              boundary_revision,
                                              result.value().state_hash.substr(0, 12));
                TEST_REQUIRE(!node->dfs()
                                  ->read_file_status(deployed_actor.id(), name, Dfs::Basic::TEMPLATE_CONTRACTS)
                                  .has_value());
            }
        }
        std::filesystem::remove(std::filesystem::path("contract-heads")
                                / (deployed_actor.id().to_string() + ".msgpack"));
        DfsContractStorage boundary_recovery(node->dfs(), node->dag());
        const auto         boundary = boundary_recovery.load(deployed_actor.id().to_string());
        TEST_REQUIRE(boundary.has_value());
        const auto& checkpoint = boundary.value().versions.back().revisions.back();
        TEST_REQUIRE_EQ(checkpoint.revision, boundary_revision);
        TEST_REQUIRE_EQ(checkpoint.checkpoint_revision, boundary_revision);
        TEST_REQUIRE_EQ(checkpoint.checkpoint_author_id, other.id().to_string());
        TEST_REQUIRE(checkpoint.checkpoint_storage_id.empty());
        TEST_REQUIRE_EQ(checkpoint.state, checkpoint_state);
    }
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
