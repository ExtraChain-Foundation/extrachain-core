#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "chain/transaction.h"
#include "consensus/consensus_protocol.h"
#include "consensus/intent_store.h"
#include "consensus/validator_set.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"

namespace {
    using namespace ExtraChain::Consensus;

    Actor<KeyPrivate> make_actor(ActorType type = ActorType::Service) {
        Actor<KeyPrivate> actor;
        actor.create(type);
        return actor;
    }

    std::vector<KeyPrivate> make_keys(std::size_t count) {
        std::vector<KeyPrivate> result(count);
        for (auto& key : result) {
            key.generate_random();
        }
        return result;
    }

    std::vector<std::string> public_keys(const std::vector<KeyPrivate>& keys) {
        std::vector<std::string> result;
        result.reserve(keys.size());
        for (const auto& key : keys) {
            result.push_back(Utils::to_base64(key.public_key()));
        }
        return result;
    }
} // namespace

int main() {
    using namespace ExtraChain::Consensus;

    int        passed = 0;
    int        failed = 0;
    const auto check  = [&](const char* name, bool result) {
        std::printf("  [%s] %s\n", result ? "PASS" : "FAIL", name);
        result ? ++passed : ++failed;
    };

    auto network      = make_actor();
    auto sender       = make_actor(ActorType::User);
    auto receiver     = make_actor(ActorType::User);
    auto other_sender = make_actor(ActorType::User);

    TransactionIntentV2 first {
        .network_id           = network.id(),
        .sender               = sender.id(),
        .receiver             = receiver.id(),
        .amount               = "1.25",
        .operation            = IntentOperation::Transfer,
        .account_nonce        = 1,
        .valid_after_height   = 10,
        .expires_after_height = 30,
    };
    const auto signed_first = make_intent(first, "memo-one", sender);
    check("intent is signed without a DAG section", signed_first.has_value());
    IntentEnvelope first_envelope { .intent = signed_first.value(), .metadata = "memo-one" };
    const auto     sender_public_key = Utils::to_base64(sender.key().public_key());
    check("signed intent verifies", verify_intent(first_envelope, sender_public_key));
    const auto materialized = materialize_intent(first_envelope, 220, 14, { "previous-hash" });
    check("consensus assigns a stable intent to a DAG section", materialized.has_value());
    check("materialized intent keeps the user authorization",
          materialized.has_value() && materialized.value().section() == SectionId(220)
              && materialized.value().verify(sender.to_public()));
    const auto restored_intent =
        materialized.has_value()
            ? intent_from_transaction(materialized.value())
            : std::expected<IntentEnvelope, ConsensusError>(std::unexpected(ConsensusError::InvalidIntent));
    check("materialized intent can be recovered from historical DAG data",
          restored_intent.has_value()
              && hash_intent(restored_intent.value().intent) == hash_intent(first_envelope.intent));
    auto changed_materialization = materialized.value();
    changed_materialization.set_amount(BigNumberFloat("2"));
    changed_materialization.update_hash();
    check("materialized intent rejects changed operation data",
          !changed_materialization.verify(sender.to_public()));

    auto changed_metadata     = first_envelope;
    changed_metadata.metadata = "changed";
    check("changed intent metadata is rejected", !verify_intent(changed_metadata, sender_public_key));
    auto malformed_amount   = first;
    malformed_amount.amount = "1e6";
    check("non-decimal intent amount is rejected", !make_intent(malformed_amount, "memo", sender).has_value());
    malformed_amount.amount = "0";
    check("zero transfer intent is rejected", !make_intent(malformed_amount, "memo", sender).has_value());

    IntentPool pool;
    const auto first_hash = pool.submit(first_envelope, sender_public_key, 0, 10);
    check("valid intent enters the bounded pool", first_hash.has_value());
    check("duplicate intent is rejected", !pool.submit(first_envelope, sender_public_key, 0, 10).has_value());

    auto second                  = first;
    second.account_nonce         = 2;
    const auto     signed_second = make_intent(second, "memo-two", sender);
    IntentEnvelope second_envelope { .intent = signed_second.value(), .metadata = "memo-two" };
    check("future sequential nonce enters the pool",
          pool.submit(second_envelope, sender_public_key, 0, 10).has_value());

    auto delayed                 = first;
    delayed.sender               = other_sender.id();
    delayed.valid_after_height   = 20;
    delayed.expires_after_height = 40;
    const auto signed_delayed    = make_intent(delayed, "delayed", other_sender);
    IntentPool delayed_pool;
    check("future-valid intent enters the pool",
          delayed_pool
              .submit(IntentEnvelope { .intent = signed_delayed.value(), .metadata = "delayed" },
                      Utils::to_base64(other_sender.key().public_key()),
                      0,
                      10)
              .has_value());
    check("future-valid intent waits for its height", delayed_pool.ready({}, 19, 1, 1'024).empty());
    check("future-valid intent becomes ready", delayed_pool.ready({}, 20, 1, 1'024).size() == 1);

    auto gap              = first;
    gap.account_nonce     = 65;
    const auto signed_gap = make_intent(gap, "gap", sender);
    check("nonce outside the bounded window is rejected",
          !pool.submit(IntentEnvelope { .intent = signed_gap.value(), .metadata = "gap" },
                       sender_public_key,
                       0,
                       10)
               .has_value());

    const auto selected = pool.ready({}, 10, 10, 1024 * 1024);
    check("one batch selects one sequential nonce for a sender",
          selected.size() == 1 && selected.front().intent.account_nonce == 1);
    const auto next_selected = pool.ready({ { sender.id(), 1 } }, 10, 10, 1024 * 1024);
    check("the next batch selects the next committed sender nonce",
          next_selected.size() == 1 && next_selected.front().intent.account_nonce == 2);

    auto contract_sender             = make_actor(ActorType::User);
    auto competing_sender            = make_actor(ActorType::User);
    auto contract_call               = first;
    contract_call.sender             = contract_sender.id();
    contract_call.amount             = "0";
    contract_call.operation          = IntentOperation::ContractCall;
    contract_call.account_nonce      = 1;
    const auto signed_contract_call  = make_intent(contract_call, "call-a", contract_sender);
    auto       competing_call        = contract_call;
    competing_call.sender            = competing_sender.id();
    const auto signed_competing_call = make_intent(competing_call, "call-b", competing_sender);
    IntentPool contract_pool;
    check("competing contract intents enter the pool",
          signed_contract_call.has_value() && signed_competing_call.has_value()
              && contract_pool
                     .submit(IntentEnvelope { .intent = signed_contract_call.value(), .metadata = "call-a" },
                             Utils::to_base64(contract_sender.key().public_key()),
                             0,
                             10)
                     .has_value()
              && contract_pool
                     .submit(IntentEnvelope { .intent = signed_competing_call.value(), .metadata = "call-b" },
                             Utils::to_base64(competing_sender.key().public_key()),
                             0,
                             10)
                     .has_value());
    check("one batch selects at most one contract mutation",
          contract_pool.ready({}, 10, 10, 1024 * 1024).size() == 1);

    auto large              = first;
    large.sender            = other_sender.id();
    large.account_nonce     = 1;
    const auto signed_large = make_intent(large, std::string(4'096, 'x'), other_sender);
    IntentPool size_limited_pool;
    check("large intent enters the pool",
          size_limited_pool
              .submit(IntentEnvelope { .intent = signed_large.value(), .metadata = std::string(4'096, 'x') },
                      Utils::to_base64(other_sender.key().public_key()),
                      0,
                      10)
              .has_value());
    check("small intent enters a pool with a large intent",
          size_limited_pool.submit(first_envelope, sender_public_key, 0, 10).has_value());
    const auto size_limited = size_limited_pool.ready({}, 10, 2, 1'024);
    check("an oversized head does not block another sender",
          size_limited.size() == 1 && size_limited.front().intent.sender == sender.id());
    pool.erase({ first_hash.value() });
    check("finalized intent leaves the pool", pool.size() == 1);
    check("expired intent leaves the pool", pool.expire(31).size() == 1 && pool.size() == 0);

    const auto store_root =
        std::filesystem::temp_directory_path() / ("extrachain-intent-store-" + Utils::generate_random_hex(8));
    const auto store_path = store_root / "intents.sqlite";
    {
        IntentStore store(store_path);
        check("intent store opens with a durable WAL", store.open().has_value());
        check("intent store persists an accepted intent", store.put(first_envelope).has_value());
        check("intent store treats an identical write as idempotent", store.put(first_envelope).has_value());
        check("intent store persists the next sender nonce", store.put(second_envelope).has_value());
        const auto accepted = store.receipt(first_hash.value());
        check("intent and accepted receipt are one durable operation",
              accepted.has_value() && accepted.value().has_value()
                  && accepted.value().value().status == IntentStatus::Accepted);
        const auto pending = store.load_pending();
        check("intent store reloads the pending envelope",
              pending.has_value() && pending.value().size() == 2
                  && hash_intent(pending.value().front().intent) == first_hash.value());
    }
    {
        IntentStore restarted(store_path);
        check("intent store reopens after restart", restarted.open().has_value());
        const auto receipt = restarted.receipt(first_hash.value());
        check("intent receipt survives restart",
              receipt.has_value() && receipt.value().has_value()
                  && receipt.value().value().status == IntentStatus::Accepted);
        IntentReceipt finalized_receipt {
            .intent_hash      = first_hash.value(),
            .status           = IntentStatus::Finalized,
            .consensus_height = 14,
            .dag_section      = 280,
            .position         = 0,
        };
        auto invalid_second_receipt        = finalized_receipt;
        invalid_second_receipt.intent_hash = "wrong-hash";
        invalid_second_receipt.position    = 1;
        check("failed batch finalization rolls back every intent",
              !restarted
                   .commit_finalized(
                       { { first_envelope, finalized_receipt }, { second_envelope, invalid_second_receipt } })
                   .has_value());
        const auto after_rollback        = restarted.load_pending();
        const auto nonces_after_rollback = restarted.load_committed_nonces();
        check("failed finalization keeps pending data and nonce",
              after_rollback.has_value() && after_rollback.value().size() == 2 && nonces_after_rollback.has_value()
                  && nonces_after_rollback.value().empty());
        check("intent finalization and nonce advance are atomic",
              restarted.commit_finalized({ { first_envelope, finalized_receipt } }).has_value());
        check("exact finalization replay is idempotent",
              restarted.commit_finalized({ { first_envelope, finalized_receipt } }).has_value());
        check("expired pending intent closes atomically",
              restarted.expire({ hash_intent(second_envelope.intent) }).has_value());
        const auto pending = restarted.load_pending();
        check("finalized intent does not return after restart", pending.has_value() && pending.value().empty());
        const auto nonces = restarted.load_committed_nonces();
        check("committed nonce survives restart",
              nonces.has_value() && nonces.value().contains(sender.id()) && nonces.value().at(sender.id()) == 1);
        const auto finalized = restarted.receipt(first_hash.value());
        check("finalized receipt replaces the accepted receipt",
              finalized.has_value() && finalized.value().has_value()
                  && finalized.value().value().status == IntentStatus::Finalized);
    }
    std::filesystem::remove_all(store_root);

    const std::vector<std::string> leaves { "tx-a", "tx-b", "tx-c", "tx-d", "tx-e" };
    const auto                     root  = merkle_root(leaves);
    const auto                     proof = make_merkle_proof(leaves, 2);
    check("Merkle proof is built", proof.has_value());
    check("Merkle proof verifies", verify_merkle_proof(leaves[2], proof.value(), root));
    check("Merkle proof rejects changed data", !verify_merkle_proof("tx-x", proof.value(), root));
    auto changed_proof                = proof.value();
    changed_proof.siblings.front()[0] = changed_proof.siblings.front()[0] == '0' ? '1' : '0';
    check("Merkle proof rejects a changed branch", !verify_merkle_proof(leaves[2], changed_proof, root));

    auto       governance_keys   = make_keys(5);
    const auto governance_policy = make_multisig_policy(network.id(), 3, public_keys(governance_keys));
    check("governance policy accepts three of five", governance_policy.has_value());
    auto changed_policy      = governance_policy.value();
    changed_policy.threshold = 2;
    check("changed governance policy hash is rejected", !verify_multisig_policy(changed_policy));
    const auto authorization = authorize_action(governance_policy.value(),
                                                7,
                                                "validator-set-change",
                                                { governance_keys[0], governance_keys[1], governance_keys[2] });
    check("three governance keys authorize an action", authorization.has_value());
    check("governance authorization verifies",
          verify_authorization(governance_policy.value(), authorization.value(), 7));
    check("old governance sequence is rejected",
          !verify_authorization(governance_policy.value(), authorization.value(), 8));
    check("two governance keys cannot authorize an action",
          !authorize_action(governance_policy.value(),
                            8,
                            "insufficient",
                            { governance_keys[0], governance_keys[1] })
               .has_value());

    ActivationManifestV1 activation {
        .network_id             = network.id(),
        .activation_height      = 2'000,
        .activation_dag_section = 4'000,
        .validator_set_hash     = "activation-set",
    };
    const auto activation_authorization =
        authorize_action(governance_policy.value(),
                         9,
                         activation_action_hash(activation),
                         { governance_keys[0], governance_keys[1], governance_keys[2] });
    activation.authorization = activation_authorization.value();
    check("future aligned Shadow activation verifies",
          verify_activation_manifest(activation, governance_policy.value(), 1'500, 9));
    const auto weak_governance_policy = make_multisig_policy(network.id(), 2, public_keys(governance_keys));
    check("activation rejects a policy weaker than three of five",
          !verify_activation_manifest(activation, weak_governance_policy.value(), 1'500, 9));
    activation.activation_dag_section = 4'001;
    check("unaligned Shadow activation is rejected",
          !verify_activation_manifest(activation, governance_policy.value(), 1'500, 9));

    EpochChangeV1 change {
        .network_id                 = network.id(),
        .current_epoch              = 4,
        .activation_epoch           = 6,
        .activation_height          = 1'000,
        .current_validator_set_hash = "current-set",
        .next_validator_set_hash    = "next-set",
        .registry_document_hash     = "exdfs-registry",
    };
    std::vector<Actor<KeyPrivate>> operators;
    std::vector<KeyPrivate>        consensus_keys;
    for (std::size_t index = 0; index < 7; ++index) {
        operators.push_back(make_actor());
        KeyPrivate key;
        key.generate_random();
        consensus_keys.push_back(key);
        change.operators.push_back(OperatorAttestation {
            .operator_id_hash     = Utils::calculate_hash("operator-" + std::to_string(index)),
            .actor_id             = operators.back().id(),
            .node_identifier      = "node-" + std::to_string(index),
            .consensus_public_key = Utils::to_base64(consensus_keys.back().public_key()),
            .document_hash        = Utils::calculate_hash("attestation-" + std::to_string(index)),
        });
    }
    const auto change_authorization =
        authorize_action(governance_policy.value(),
                         8,
                         epoch_change_action_hash(change),
                         { governance_keys[0], governance_keys[1], governance_keys[2] });
    change.authorization = change_authorization.value();
    check("two-step epoch change verifies", verify_epoch_change(change, governance_policy.value(), 900, 8));
    auto duplicate_operator                          = change;
    duplicate_operator.operators[1].operator_id_hash = duplicate_operator.operators[0].operator_id_hash;
    duplicate_operator.authorization.action_hash     = epoch_change_action_hash(duplicate_operator);
    check("duplicate operator identity is rejected",
          !verify_epoch_change(duplicate_operator, governance_policy.value(), 900, 8));

    ValidatorSet governed_set {
        .network_id = network.id(),
        .epoch      = 6,
    };
    for (std::size_t index = 0; index < operators.size(); ++index) {
        const auto record = make_validator_record(network.id(),
                                                  6,
                                                  operators[index],
                                                  consensus_keys[index],
                                                  "node-" + std::to_string(index),
                                                  0);
        governed_set.validators.push_back(record.value());
    }
    const auto governed_action =
        governed_validator_set_action_hash(governed_set, change.registry_document_hash, change.operators);
    const auto governed_authorization =
        authorize_action(governance_policy.value(),
                         10,
                         governed_action,
                         { governance_keys[0], governance_keys[1], governance_keys[2] });
    const auto governed_view = ValidatorSetView::create_governed(governed_set,
                                                                 change.registry_document_hash,
                                                                 change.operators,
                                                                 governance_policy.value(),
                                                                 governed_authorization.value(),
                                                                 10);
    check("multisig creates a governed validator set without a network-actor signature",
          governed_view.has_value() && governed_view.value().active().size() == 7);
    check("governed validator set rejects a repeated authorization sequence",
          !ValidatorSetView::create_governed(governed_set,
                                             change.registry_document_hash,
                                             change.operators,
                                             governance_policy.value(),
                                             governed_authorization.value(),
                                             11)
               .has_value());

    auto transition_change                    = change;
    transition_change.next_validator_set_hash = hash_validator_set(governed_set);
    transition_change.authorization =
        authorize_action(governance_policy.value(),
                         11,
                         epoch_change_action_hash(transition_change),
                         { governance_keys[0], governance_keys[1], governance_keys[2] })
            .value();
    const auto transitioned_view = ValidatorSetView::create_epoch_transition(governed_set,
                                                                             transition_change,
                                                                             governance_policy.value(),
                                                                             900,
                                                                             11);
    check("finalized epoch data creates the next seven-validator view",
          transitioned_view.has_value() && transitioned_view.value().document().epoch == 6);

    auto               recovery_keys   = make_keys(5);
    const auto         recovery_policy = make_multisig_policy(network.id(), 4, public_keys(recovery_keys));
    RecoveryDocumentV1 recovery {
        .network_id                = network.id(),
        .recovery_sequence         = 3,
        .finalized_height          = 700,
        .finalized_checkpoint_hash = "checkpoint-700",
        .next_validator_set_hash   = "recovery-set",
        .registry_document_hash    = "recovery-registry",
    };
    const auto recovery_authorization =
        authorize_action(recovery_policy.value(),
                         3,
                         recovery_action_hash(recovery),
                         { recovery_keys[0], recovery_keys[1], recovery_keys[2], recovery_keys[3] });
    recovery.authorization = recovery_authorization.value();
    check("four of five recovery document verifies",
          verify_recovery_document(recovery, recovery_policy.value(), 3, 700, "checkpoint-700"));
    check("recovery cannot move from another checkpoint",
          !verify_recovery_document(recovery, recovery_policy.value(), 3, 700, "checkpoint-699"));

    StateCommitmentV2 commitment {
        .network_id                = network.id(),
        .epoch                     = 4,
        .height                    = 700,
        .previous_state_commitment = "previous",
        .section_root              = "sections",
        .account_state_root        = "accounts",
        .contract_state_root       = "contracts",
        .token_registry_root       = "tokens",
        .validator_set_hash        = "validators",
    };
    const auto commitment_hash    = hash_state_commitment(commitment);
    commitment.account_state_root = "changed";
    check("state commitment binds all state roots", commitment_hash != hash_state_commitment(commitment));

    std::printf("CONSENSUS PROTOCOL: %d pass, %d fail\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
