#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "consensus/consensus_engine.h"
#include "consensus/light_client.h"
#include "consensus/mining_epoch.h"
#include "consensus/mining_state.h"
#include "consensus/mining_settlement.h"
#include "consensus/mining_transaction.h"
#include "consensus/mining_replay.h"
#include "chain/dag_cache.h"
#include "chain/dag.h"
#include "utils/serialization.h"
#include "consensus/balance_snapshot.h"
#include "consensus/validator_set.h"
#include "utils/exc_utils.h"

bool test_balance_snapshot_runtime(const ExtraChain::Consensus::BalanceSnapshotV1& snapshot,
                                   const ExtraChain::Consensus::BalanceSnapshotV1& newer,
                                   const ExtraChain::Consensus::ValidatorSet&      validators);

namespace {
    using namespace ExtraChain::Consensus;

    struct Committee {
        Actor<KeyPrivate>              governance;
        std::vector<Actor<KeyPrivate>> actors;
        std::vector<KeyPrivate>        keys;
        ValidatorSet                   document;
        ValidatorSetView               view;

        Committee()
            : view(create(1)) {
        }

        Committee(const Actor<KeyPrivate>& network, std::uint64_t epoch)
            : governance(network)
            , view(create(epoch)) {
        }

        ValidatorSetView create(std::uint64_t epoch) {
            if (governance.empty()) {
                governance.create(ActorType::Service);
            }
            std::vector<ValidatorRecord> records;
            for (std::size_t index = 0; index < 7; ++index) {
                Actor<KeyPrivate> actor;
                actor.create(ActorType::Service);
                KeyPrivate key;
                key.generate_random();
                auto record = make_validator_record(governance.id(),
                                                    epoch,
                                                    actor,
                                                    key,
                                                    "validator-" + std::to_string(index),
                                                    0);
                if (!record.has_value()) {
                    std::abort();
                }
                actors.push_back(actor);
                keys.push_back(key);
                records.push_back(record.value());
            }
            auto validator_set = make_validator_set(governance.id(), epoch, std::move(records), governance);
            if (!validator_set.has_value()) {
                std::abort();
            }
            document    = validator_set.value();
            auto result = ValidatorSetView::create(document);
            if (!result.has_value()) {
                std::abort();
            }
            return result.value();
        }

        std::size_t index_for(std::string_view validator_id) const {
            for (std::size_t index = 0; index < keys.size(); ++index) {
                if (validator_id_for(keys[index].public_key()) == validator_id) {
                    return index;
                }
            }
            std::abort();
        }
    };

    SectionBatchData batch_data(const Proposal& proposal) {
        std::vector<std::pair<std::uint64_t, std::string>> sections;
        for (auto section = proposal.batch.first_section; section <= proposal.batch.last_section; ++section) {
            sections.emplace_back(section,
                                  "network-section-" + std::to_string(proposal.header.height) + '-'
                                      + std::to_string(section));
        }
        return SectionBatchData {
            .header_hash = hash_header(proposal.header),
            .manifest    = proposal.batch,
            .sections    = std::move(sections),
        };
    }

    SectionBatchManifest batch_manifest(std::uint64_t height, std::string extra_hash = {}) {
        const auto                                         first = (height - 1) * ShadowSectionInterval + 1;
        std::vector<std::pair<std::uint64_t, std::string>> sections;
        std::uint64_t                                      payload_bytes = 0;
        for (auto section = first; section < first + ShadowSectionInterval; ++section) {
            auto bytes = "network-section-" + std::to_string(height) + '-' + std::to_string(section);
            payload_bytes += bytes.size();
            sections.emplace_back(section, std::move(bytes));
        }
        std::vector<std::string> hashes {
            "network-transaction-" + std::to_string(height) + "-a",
            "network-transaction-" + std::to_string(height) + "-b",
        };
        if (!extra_hash.empty()) {
            hashes.push_back(std::move(extra_hash));
        }
        return SectionBatchManifest {
            .first_section      = first,
            .last_section       = first + ShadowSectionInterval - 1,
            .transaction_hashes = hashes,
            .transaction_root   = calculate_transaction_root(hashes),
            .data_root          = calculate_data_root(sections),
            .previous_section_root =
                height == 1 ? "activation-root" : "network-root-" + std::to_string(height - 1),
            .payload_bytes = payload_bytes,
        };
    }

    SectionBatchManifest epoch_batch_manifest(std::uint64_t height,
                                              std::uint64_t first,
                                              std::string   previous_root,
                                              std::string   extra_hash = {}) {
        std::vector<std::pair<std::uint64_t, std::string>> sections;
        std::uint64_t                                      payload_bytes = 0;
        for (auto section = first; section < first + ShadowSectionInterval; ++section) {
            auto bytes = "network-section-" + std::to_string(height) + '-' + std::to_string(section);
            payload_bytes += bytes.size();
            sections.emplace_back(section, std::move(bytes));
        }
        std::vector<std::string> hashes {
            "epoch-transaction-" + std::to_string(height) + "-a",
            "epoch-transaction-" + std::to_string(height) + "-b",
        };
        if (!extra_hash.empty()) {
            hashes.push_back(std::move(extra_hash));
        }
        return SectionBatchManifest {
            .first_section         = first,
            .last_section          = first + ShadowSectionInterval - 1,
            .transaction_hashes    = hashes,
            .transaction_root      = calculate_transaction_root(hashes),
            .data_root             = calculate_data_root(sections),
            .previous_section_root = std::move(previous_root),
            .payload_bytes         = payload_bytes,
        };
    }

    StateCommitmentV2 state_commitment(ConsensusEngine& engine, std::uint64_t height, std::string section_root) {
        std::string previous;
        const auto& parent = engine.safety_state().highest_certificate;
        if (parent.has_value() && parent.value().phase != Phase::Genesis) {
            previous = engine.proposal_for(parent.value().header_hash).value().header.state_commitment;
        } else if (engine.epoch_bootstrap().has_value()) {
            previous = engine.epoch_bootstrap().value().previous_state_commitment;
        }
        return StateCommitmentV2 {
            .network_id                = engine.validators().document().network_id,
            .epoch                     = engine.validators().document().epoch,
            .height                    = height,
            .previous_state_commitment = std::move(previous),
            .section_root              = std::move(section_root),
            .account_state_root        = balance_snapshot_root(
                { { { engine.validators().document().network_id, ActorId { } }, BigNumberFloat(25) } }),
            .contract_state_root = "contract-root-" + std::to_string(height),
            .token_registry_root = "token-root-" + std::to_string(height),
            .validator_set_hash  = engine.validators().hash(),
        };
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

    Committee                committee;
    Committee                next_committee(committee.governance, 3);
    Committee                third_committee(committee.governance, 5);
    std::vector<KeyPrivate>  governance_keys(5);
    std::vector<std::string> governance_public_keys;
    for (auto& key : governance_keys) {
        key.generate_random();
        governance_public_keys.push_back(Utils::to_base64(key.public_key()));
    }
    const auto governance_policy =
        make_multisig_policy(committee.governance.id(), GovernanceThreshold, governance_public_keys);
    EpochChangeV1 epoch_change {
        .network_id                 = committee.governance.id(),
        .current_epoch              = 1,
        .activation_epoch           = 3,
        .activation_height          = 13,
        .current_validator_set_hash = committee.view.hash(),
        .next_validator_set_hash    = next_committee.view.hash(),
        .registry_document_hash     = Utils::calculate_hash("network-epoch-3-registry"),
    };
    for (std::size_t index = 0; index < next_committee.actors.size(); ++index) {
        epoch_change.operators.push_back(OperatorAttestation {
            .operator_id_hash     = Utils::calculate_hash("network-operator-" + std::to_string(index)),
            .actor_id             = next_committee.actors[index].id(),
            .node_identifier      = "validator-" + std::to_string(index),
            .consensus_public_key = Utils::to_base64(next_committee.keys[index].public_key()),
            .document_hash        = Utils::calculate_hash("network-attestation-" + std::to_string(index)),
        });
    }
    epoch_change.authorization = authorize_action(governance_policy.value(),
                                                  20,
                                                  epoch_change_action_hash(epoch_change),
                                                  { governance_keys[0], governance_keys[1], governance_keys[2] })
                                     .value();
    const auto    epoch_action_hash = epoch_change_action_hash(epoch_change);
    EpochChangeV1 third_epoch_change {
        .network_id                 = committee.governance.id(),
        .current_epoch              = 3,
        .activation_epoch           = 5,
        .activation_height          = 16,
        .current_validator_set_hash = next_committee.view.hash(),
        .next_validator_set_hash    = third_committee.view.hash(),
        .registry_document_hash     = Utils::calculate_hash("network-epoch-5-registry"),
    };
    for (std::size_t index = 0; index < third_committee.actors.size(); ++index) {
        third_epoch_change.operators.push_back(OperatorAttestation {
            .operator_id_hash     = Utils::calculate_hash("network-epoch-5-operator-" + std::to_string(index)),
            .actor_id             = third_committee.actors[index].id(),
            .node_identifier      = "validator-" + std::to_string(index),
            .consensus_public_key = Utils::to_base64(third_committee.keys[index].public_key()),
            .document_hash        = Utils::calculate_hash("network-epoch-5-attestation-" + std::to_string(index)),
        });
    }
    third_epoch_change.authorization =
        authorize_action(governance_policy.value(),
                         21,
                         epoch_change_action_hash(third_epoch_change),
                         { governance_keys[0], governance_keys[1], governance_keys[2] })
            .value();
    const auto third_epoch_action_hash = epoch_change_action_hash(third_epoch_change);
    const auto root =
        std::filesystem::temp_directory_path() / ("extrachain-shadow-network-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(root);

    std::vector<std::unique_ptr<ConsensusEngine>> engines;
    const auto                                    make_engine = [&](std::size_t index) {
        auto engine =
            std::make_unique<ConsensusEngine>(committee.view,
                                              ValidatorIdentity { .validator_id = validator_id_for(
                                                                      committee.keys[index].public_key()),
                                                                                                     .key = committee.keys[index] },
                                              std::make_unique<SafetyStore>(
                                                  root / ("validator-" + std::to_string(index) + ".sqlite")));
        if (!engine->initialize().has_value()) {
            std::abort();
        }
        return engine;
    };
    for (std::size_t index = 0; index < committee.keys.size(); ++index) {
        engines.push_back(make_engine(index));
    }

    const auto reward_reader = [](std::uint64_t) -> std::expected<std::string, ConsensusError> {
        return "certified storage";
    };
    const auto reward_dataset    = commit_storage_dataset(17, reward_reader).value();
    const auto reward_dataset_id = storage_dataset_id(committee.governance.id(), reward_dataset).value();
    const MiningEmissionPolicy certified_policy { 0, { { 1, 10 } } };
    auto certified_mining = configure_mining_state(committee.governance.id(), 0, certified_policy).value();
    check("certified mining registers immutable bytes before its epoch",
          register_storage_provider(certified_mining, committee.governance.id(), reward_dataset).has_value());
    auto       mining_verifier = LightClientVerifier::create(committee.document).value();
    const auto mining_finality = [&](std::uint64_t section) -> std::expected<FinalityProof, ConsensusError> {
        const auto proofs = engines.front()->finality_proofs_after(section / ShadowSectionInterval - 1, 1);
        if (!proofs.has_value() || proofs.value().size() != 1
            || proofs.value().front().finalized_proposal.header.dag_section != section)
            return std::unexpected(ConsensusError::DataUnavailable);
        return proofs.value().front();
    };
    std::optional<MiningEpochWitness> closing_witness;
    std::optional<MiningState>        before_settlement;
    std::string                       after_settlement_root;
    MiningPayouts                     certified_payouts;
    bool quorum_guard_checked = false;
    for (std::uint64_t height = 1; height <= 12; ++height) {
        const auto& leader       = committee.view.leader(height, 0);
        const auto  leader_index = committee.index_for(leader.validator_id);
        for (auto section = (height - 1) * ShadowSectionInterval + 1; section <= height * ShadowSectionInterval;
             ++section) {
            const auto payouts = advance_mining_state(certified_mining,
                                                      section,
                                                      section == 1 ? 10 : 0,
                                                      mining_finality,
                                                      mining_verifier);
            check("mining projects each section before certification", payouts.has_value());
            if (!payouts.has_value())
                std::abort();
            for (const auto& [provider, units] : payouts.value())
                certified_payouts[provider] += units;
            if (section == 81) {
                const auto proof = make_storage_proof(committee.governance.id(),
                                                      committee.governance.id(),
                                                      reward_dataset,
                                                      certified_mining.epochs.at(0).challenge.value(),
                                                      reward_reader)
                                       .value();
                check("certified mining accepts the challenged bytes in its proof window",
                      submit_mining_proof(certified_mining, 0, committee.governance.id(), reward_dataset_id, proof)
                          .has_value());
            }
        }
        if (height == 6)
            closing_witness = make_mining_epoch_witness(certified_mining, 0).value();
        if (height == 8)
            before_settlement = certified_mining;
        if (height == 9)
            after_settlement_root = mining_state_root(certified_mining);
        auto projected_state =
            state_commitment(*engines[leader_index], height, "network-root-" + std::to_string(height));
        projected_state.mining_state_root = mining_state_root(certified_mining);
        const auto proposal = engines[leader_index]->make_proposal(batch_manifest(height,
                                                                                  height == 10 ? epoch_action_hash
                                                                                               : std::string { }),
                                                                   std::move(projected_state));
        check("scheduled leader proposes a canonical network batch", proposal.has_value());
        if (!proposal.has_value()) {
            break;
        }

        const auto               data = batch_data(proposal.value());
        std::vector<std::size_t> delivery_order(engines.size());
        std::iota(delivery_order.begin(), delivery_order.end(), 0);
        std::rotate(delivery_order.begin(),
                    delivery_order.begin() + static_cast<std::ptrdiff_t>(height % delivery_order.size()),
                    delivery_order.end());

        std::vector<Vote> votes;
        for (const auto index : delivery_order) {
            if (index != leader_index) {
                check("validator observes a network proposal",
                      engines[index]->observe_proposal(proposal.value()).has_value());
            }
            check("validator durably stages network data before voting",
                  engines[index]->stage_batch(data).has_value());
            const auto vote = engines[index]->accept_proposal(proposal.value());
            check("validator votes for a safe staged network proposal", vote.has_value());
            if (vote.has_value()) {
                votes.push_back(vote.value());
            }
        }

        std::reverse(votes.begin(), votes.end());
        std::optional<QuorumCertificate> certificate;
        for (std::size_t index = 0; index < votes.size(); ++index) {
            const auto accepted = engines[leader_index]->accept_vote(votes[index]);
            check("leader accepts reordered network vote", accepted.has_value());
            if (!quorum_guard_checked && index == 3) {
                check("four of seven votes cannot form a certificate",
                      accepted.has_value() && !accepted.value().certificate.has_value());
                quorum_guard_checked = true;
            }
            if (accepted.has_value() && accepted.value().certificate.has_value()) {
                certificate = accepted.value().certificate;
            }
        }
        check("five of seven votes form a certificate", certificate.has_value());
        if (!certificate.has_value()) {
            break;
        }

        for (auto& engine : engines) {
            check("all connected validators accept the certificate",
                  engine->accept_certificate(certificate.value()).has_value());
        }

        if (height % 4 == 0) {
            const auto restart_index = static_cast<std::size_t>((height / 4) % engines.size());
            const auto state_before  = engines[restart_index]->safety_state();
            engines[restart_index].reset();
            engines[restart_index] = make_engine(restart_index);
            check("restarted validator restores the certified height",
                  engines[restart_index]->safety_state().highest_certificate.has_value()
                      && state_before.highest_certificate.has_value()
                      && engines[restart_index]->safety_state().highest_certificate.value().height
                             == state_before.highest_certificate.value().height);
            check("restarted validator restores finality",
                  engines[restart_index]->safety_state().finalized_height == state_before.finalized_height);
        }
    }

    MiningSettlementRecord settlement { closing_witness.value(), mining_finality(120).value() };
    const auto             checked_payouts = verify_mining_settlement(settlement, 161, mining_verifier);
    check("settlement derives exact payouts from a certified mining root",
          checked_payouts.has_value() && checked_payouts.value() == certified_payouts
              && certified_payouts.at(committee.governance.id().to_string()) == 10);
    const auto settlement_transaction = make_mining_settlement_transaction(settlement).value();
    const auto settlement_batch       = [&](const std::optional<Transaction>& record) {
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        SectionBatchData  batch;
        batch.manifest.first_section = 161;
        batch.manifest.last_section  = 180;
        for (std::uint64_t section = 161; section <= 180; ++section) {
            Section content { .id = SectionId(section) };
            if (section == 161 && record.has_value())
                content.transactions.insert(record.value());
            batch.sections.emplace_back(section, Json::serialize(content));
        }
        return batch;
    };
    const auto replayed = replay_mining_batch(before_settlement.value(),
                                              certified_policy,
                                              settlement_batch(settlement_transaction),
                                              mining_finality,
                                              mining_verifier);
    check("DAG batch replay requires and applies the exact certified settlement",
          replayed.has_value() && mining_state_root(replayed.value()) == after_settlement_root);
    check("DAG batch replay rejects an omitted settlement",
          !replay_mining_batch(before_settlement.value(),
                               certified_policy,
                               settlement_batch(std::nullopt),
                               mining_finality,
                               mining_verifier)
               .has_value());
    check("DAG batch replay rejects a substituted emission policy",
          !replay_mining_batch(before_settlement.value(),
                               MiningEmissionPolicy { 0, { { 1, 11 } } },
                               settlement_batch(settlement_transaction),
                               mining_finality,
                               mining_verifier)
               .has_value());
    const auto restored_mining =
        MessagePack::deserialize<MiningState>(MessagePack::serialize(before_settlement.value()));
    check("restored mining state replays the same batch",
          restored_mining.has_value()
              && mining_state_root(replay_mining_batch(restored_mining.value(),
                                                       certified_policy,
                                                       settlement_batch(settlement_transaction),
                                                       mining_finality,
                                                       mining_verifier)
                                       .value())
                     == after_settlement_root);
    check("mining batch cannot be applied twice",
          !replay_mining_batch(replayed.value(),
                               certified_policy,
                               settlement_batch(settlement_transaction),
                               mining_finality,
                               mining_verifier)
               .has_value());
    check("native system transaction verifies without an actor mint signature",
          verify_mining_settlement_transaction(settlement_transaction, committee.governance.id(), mining_verifier)
                  .value()
              == certified_payouts);
    DagCache native_cache(nullptr, nullptr);
    Balances native_balances;
    native_cache.process_transaction(settlement_transaction, native_balances);
    check("native balance replay credits exact ExC units",
          native_balances.size() == 1
              && native_balances.at({ committee.governance.id(), ActorId { } })
                     == BigNumberFloat::create("0.00000010").value());
    auto  alternative_settlement          = settlement;
    auto& alternative_certificate         = alternative_settlement.closure.decision_certificate;
    alternative_certificate.signer_bitmap = { 0x1f };
    alternative_certificate.signatures.clear();
    for (std::size_t index = 0; index < 5; ++index) {
        const auto& validator = committee.view.active()[index];
        Vote        vote { .network_id   = alternative_certificate.network_id,
                           .epoch        = alternative_certificate.epoch,
                           .height       = alternative_certificate.height,
                           .round        = alternative_certificate.round,
                           .phase        = alternative_certificate.phase,
                           .header_hash  = alternative_certificate.header_hash,
                           .validator_id = validator.validator_id };
        const auto  signature = committee.keys[committee.index_for(validator.validator_id)].sign(
            ByteArray(vote_signing_payload(vote)).toBytes());
        alternative_certificate.signatures.push_back(Utils::to_base64(signature.value()));
    }
    const auto alternative_transaction = make_mining_settlement_transaction(alternative_settlement).value();
    check("different valid quorum subsets cannot change the settlement identity",
          alternative_transaction.meta() != settlement_transaction.meta()
              && alternative_transaction.hash() == settlement_transaction.hash()
              && verify_mining_settlement_transaction(alternative_transaction,
                                                      committee.governance.id(),
                                                      mining_verifier)
                         .value()
                     == certified_payouts);
    for (unsigned mutation = 0; mutation < 7; ++mutation) {
        auto invalid_transaction = settlement_transaction;
        if (mutation == 0)
            invalid_transaction.set_amount(BigNumberFloat(1));
        if (mutation == 1)
            invalid_transaction.set_sender(committee.governance.id());
        if (mutation == 2)
            invalid_transaction.set_token(committee.governance.id());
        if (mutation == 3)
            invalid_transaction.set_timestamp(1);
        if (mutation == 4)
            invalid_transaction.set_prev_hashs({ "extra-parent" });
        if (mutation == 5)
            invalid_transaction.set_section(SectionId(162));
        if (mutation == 6)
            invalid_transaction.set_meta(std::string(12 * 1024 * 1024, 'a'));
        invalid_transaction.update_hash();
        check("native system transaction rejects altered canonical fields",
              !verify_mining_settlement_transaction(invalid_transaction,
                                                    committee.governance.id(),
                                                    mining_verifier)
                   .has_value());
        native_cache.process_transaction(invalid_transaction, native_balances);
        check("invalid native record has no balance replay effect",
              native_balances.at({ committee.governance.id(), ActorId { } })
                  == BigNumberFloat::create("0.00000010").value());
    }
    check("settlement rejects the wrong section",
          !verify_mining_settlement(settlement, 160, mining_verifier).has_value()
              && !verify_mining_settlement(settlement, 162, mining_verifier).has_value());
    auto changed_settlement = settlement;
    changed_settlement.witness.epoch.budget_units += 1;
    check("settlement rejects an altered epoch budget",
          !verify_mining_settlement(changed_settlement, 161, mining_verifier).has_value());
    changed_settlement                                                    = settlement;
    changed_settlement.closure.finalized_proposal.state.mining_state_root = mining_state_root(certified_mining);
    check("settlement rejects a replaced signed state root",
          !verify_mining_settlement(changed_settlement, 161, mining_verifier).has_value());
    changed_settlement = settlement;
    changed_settlement.closure.decision_certificate.signatures.clear();
    check("settlement rejects missing quorum signatures",
          !verify_mining_settlement(changed_settlement, 161, mining_verifier).has_value());
    const auto restored_settlement =
        MessagePack::deserialize<MiningSettlementRecord>(MessagePack::serialize(settlement));
    check("settlement restores with the same verified native payout",
          restored_settlement.has_value()
              && verify_mining_settlement(restored_settlement.value(), 161, mining_verifier).value()
                     == certified_payouts);

    const auto expected_finalized = std::uint64_t(10);
    check("seven-node run finalizes the three-chain prefix",
          std::ranges::all_of(engines, [expected_finalized](const auto& engine) {
              return engine->safety_state().finalized_height == expected_finalized;
          }));
    const auto inclusion = engines.front()->transaction_inclusion_proof("network-transaction-1-b");
    check("old finalized network transaction has an inclusion proof",
          inclusion.has_value() && inclusion.value().has_value()
              && engines.front()->verify_transaction_inclusion_proof(inclusion.value().value()));
    auto light_client = LightClientVerifier::create(committee.document);
    check("light client starts from the trusted validator set", light_client.has_value());
    check("light client verifies finality and transaction inclusion without DAG state",
          light_client.has_value() && inclusion.has_value() && inclusion.value().has_value()
              && light_client.value().verify_transaction_proof(inclusion.value().value()));
    BalanceSnapshotV1 balance_snapshot {
        .balances = { { { committee.governance.id(), ActorId { } }, BigNumberFloat(25) } },
        .proof    = inclusion.value().value().finality_proof,
    };
    check("light client verifies balances against the certified state root",
          verify_balance_snapshot(balance_snapshot, light_client.value()));
    auto altered_balance = balance_snapshot;
    altered_balance.balances.begin()->second += BigNumberFloat(1);
    check("light client rejects a changed balance",
          !verify_balance_snapshot(altered_balance, light_client.value()));
    altered_balance.balances.clear();
    check("light client rejects omitted balances",
          !verify_balance_snapshot(altered_balance, light_client.value()));
    altered_balance                                                   = balance_snapshot;
    altered_balance.proof.finalized_proposal.state.account_state_root = balance_snapshot_root({ });
    altered_balance.balances.clear();
    check("light client rejects a replaced root", !verify_balance_snapshot(altered_balance, light_client.value()));
    altered_balance = balance_snapshot;
    altered_balance.proof.decision_certificate.signatures.clear();
    check("light client rejects a missing finality signature",
          !verify_balance_snapshot(altered_balance, light_client.value()));
    const auto newer_proofs =
        engines.front()->finality_proofs_after(balance_snapshot.proof.finalized_proposal.header.height, 1);
    check("newer finalized snapshot is available", newer_proofs.has_value() && !newer_proofs.value().empty());
    BalanceSnapshotV1 newer_snapshot { balance_snapshot.balances, newer_proofs.value().front() };
    const auto        storage_reader = [](std::uint64_t) -> std::expected<std::string, ConsensusError> {
        return "a";
    };
    const auto dataset    = commit_storage_dataset(1, storage_reader).value();
    const auto dataset_id = storage_dataset_id(committee.governance.id(), dataset).value();
    auto       mining =
        freeze_mining_epoch(committee.governance.id(), 0, 10, { { committee.governance.id(), dataset, 0 } })
            .value();
    check("mining rejects a checkpoint from the wrong section",
          !open_mining_proof_window(mining, balance_snapshot.proof, light_client.value()).has_value());
    auto bad_mining_checkpoint = newer_snapshot.proof;
    bad_mining_checkpoint.decision_certificate.signatures.clear();
    check("mining rejects an unauthenticated challenge checkpoint",
          !open_mining_proof_window(mining, bad_mining_checkpoint, light_client.value()).has_value());
    check("mining opens its fixed window from authenticated Shadow finality",
          open_mining_proof_window(mining, newer_snapshot.proof, light_client.value()).has_value()
              && mining.proof_first_section == 81 && mining.proof_last_section == 120);
    const auto storage_proof = make_storage_proof(committee.governance.id(),
                                                  committee.governance.id(),
                                                  dataset,
                                                  mining.challenge.value(),
                                                  storage_reader)
                                   .value();
    check("mining accepts the registered provider within the verified window",
          accept_mining_proof(mining, committee.governance.id(), dataset_id, 81, storage_proof).has_value());
    const auto closing_proofs = engines.front()->finality_proofs_after(5, 1);
    check("mining window closure has a finality proof",
          closing_proofs.has_value() && closing_proofs.value().size() == 1);
    check("mining rejects finality before the window closes",
          !settle_mining_epoch(mining, newer_snapshot.proof, light_client.value()).has_value());
    auto bad_closing_checkpoint = closing_proofs.value().front();
    bad_closing_checkpoint.decision_certificate.signatures.clear();
    check("mining rejects a forged settlement certificate",
          !settle_mining_epoch(mining, bad_closing_checkpoint, light_client.value()).has_value());
    check("mining settles only after authenticated window finality",
          settle_mining_epoch(mining, closing_proofs.value().front(), light_client.value()).has_value()
              && mining.rewards.at(committee.governance.id().to_string()) == 10);
    auto       ledger         = create_mining_state(committee.governance.id(), 0).value();
    const auto other_provider = committee.actors.front().id();
    check("mining registry accepts a provider before the epoch",
          register_storage_provider(ledger, committee.governance.id(), dataset).has_value());
    const auto registered_root = mining_state_root(ledger);
    check("mining registry rejects duplicate aliases without a state change",
          !register_storage_provider(ledger, committee.governance.id(), dataset).has_value()
              && mining_state_root(ledger) == registered_root);
    const MiningFinalityReader read_finality =
        [&](std::uint64_t section) -> std::expected<FinalityProof, ConsensusError> {
        const auto proof = engines.front()->finality_proof_for_section(section);
        if (!proof.has_value() || !proof.value().has_value())
            return std::unexpected(ConsensusError::DataUnavailable);
        return proof.value().value();
    };
    for (std::uint64_t section = 1; section <= 80; ++section) {
        const auto budget   = section == 1 ? 7 : section == 21 ? 5 : 0;
        const auto advanced = advance_mining_state(ledger, section, budget, read_finality, light_client.value());
        check("mining reserves only the scheduled epoch budgets before proofs",
              advanced.has_value() && advanced.value().empty() && ledger.minted_units == 0);
        if (section == 1)
            check("mining registers a second provider after the first epoch is frozen",
                  register_storage_provider(ledger, other_provider, dataset).has_value());
        if (section == 20)
            check("mining withdrawal leaves the active epoch snapshot intact",
                  unregister_storage_provider(ledger, committee.governance.id(), dataset_id).has_value()
                      && ledger.epochs.at(0)
                             .datasets.at(dataset_id)
                             .providers.contains(committee.governance.id().to_string()));
    }
    check("mining freezes eligibility independently for successive epochs",
          ledger.reserved_units == 12 && ledger.epochs.size() == 2
              && !ledger.epochs.at(0).datasets.at(dataset_id).providers.contains(other_provider.to_string())
              && !ledger.epochs.at(1)
                      .datasets.at(dataset_id)
                      .providers.contains(committee.governance.id().to_string()));
    const auto saved_ledger       = MessagePack::serialize(ledger);
    const auto root_before_window = mining_state_root(ledger);
    check("missing finality leaves the entire mining transition unchanged",
          !advance_mining_state(ledger, 81, 0, { }, light_client.value()).has_value()
              && mining_state_root(ledger) == root_before_window);
    MiningPayouts expected_payouts;
    std::string   expected_mining_root;
    for (int replay = 0; replay < 2; ++replay) {
        if (replay != 0) {
            auto restored = MessagePack::deserialize<MiningState>(saved_ledger);
            check("mining state restores from its persisted encoding", restored.has_value());
            ledger = std::move(restored.value());
        }
        MiningPayouts payouts;
        for (std::uint64_t section = 81; section <= 181; ++section) {
            if (section == 161) {
                const auto                 before = mining_state_root(ledger);
                const MiningFinalityReader forged =
                    [&](auto target) -> std::expected<FinalityProof, ConsensusError> {
                    auto proof = read_finality(target).value();
                    proof.decision_certificate.signatures.clear();
                    return proof;
                };
                check("forged closing finality cannot credit or prune mining state",
                      !advance_mining_state(ledger, section, 0, forged, light_client.value()).has_value()
                          && mining_state_root(ledger) == before);
                check("a failed reservation cannot partially settle an earlier epoch",
                      !advance_mining_state(ledger,
                                            section,
                                            MaximumMiningEmissionUnits,
                                            read_finality,
                                            light_client.value())
                              .has_value()
                          && mining_state_root(ledger) == before);
            }
            const auto advanced = advance_mining_state(ledger, section, 0, read_finality, light_client.value());
            check("mining advances and settles from verified finality", advanced.has_value());
            if (advanced.has_value())
                for (const auto& [provider, amount] : advanced.value())
                    payouts[provider] += amount;
            if (section == 81 || section == 101) {
                const auto epoch    = section == 81 ? 0 : 1;
                const auto provider = epoch == 0 ? committee.governance.id() : other_provider;
                const auto proof    = make_storage_proof(committee.governance.id(),
                                                         provider,
                                                         dataset,
                                                         ledger.epochs.at(epoch).challenge.value(),
                                                         storage_reader)
                                          .value();
                if (epoch == 0)
                    check("a late provider cannot claim the earlier epoch",
                          !submit_mining_proof(ledger, epoch, other_provider, dataset_id, proof).has_value());
                check("the frozen provider submits one proof",
                      submit_mining_proof(ledger, epoch, provider, dataset_id, proof).has_value());
                const auto before = mining_state_root(ledger);
                check("duplicate mining proof has no state effect",
                      !submit_mining_proof(ledger, epoch, provider, dataset_id, proof).has_value()
                          && mining_state_root(ledger) == before);
            }
            if (section < 161)
                check("mining cannot pay before finalized window closure", payouts.empty());
        }
        check("mining settles both epochs and prunes completed proof records",
              ledger.epochs.empty() && ledger.minted_units == 12 && ledger.reserved_units == 12
                  && payouts[committee.governance.id().to_string()] == 7
                  && payouts[other_provider.to_string()] == 5);
        if (replay == 0) {
            expected_payouts     = payouts;
            expected_mining_root = mining_state_root(ledger);
        } else {
            check("mining replay returns identical native payouts and state",
                  payouts == expected_payouts && mining_state_root(ledger) == expected_mining_root);
        }
    }
    const auto settled_root = mining_state_root(ledger);
    check("settlement cannot run twice for one section",
          !advance_mining_state(ledger, 181, 0, read_finality, light_client.value()).has_value()
              && mining_state_root(ledger) == settled_root);
    auto capped = create_mining_state(committee.governance.id(), 0).value();
    check("an unused epoch consumes its reservation without emission",
          advance_mining_state(capped, 1, MaximumMiningEmissionUnits, { }, light_client.value()).has_value()
              && capped.epochs.empty() && capped.minted_units == 0);
    for (std::uint64_t section = 2; section <= 20; ++section)
        check("an empty mining interval needs no proof traffic",
              advance_mining_state(capped, section, 0, { }, light_client.value()).has_value());
    const auto capped_root = mining_state_root(capped);
    check("unused mining allocations cannot be carried past the total cap",
          !advance_mining_state(capped, 21, 1, { }, light_client.value()).has_value()
              && mining_state_root(capped) == capped_root);
    check("mining proceeds without new emission after the cap",
          advance_mining_state(capped, 21, 0, { }, light_client.value()).has_value());
    check("runtime accepts only requested certified light balances and rejects rollback",
          test_balance_snapshot_runtime(balance_snapshot, newer_snapshot, committee.document));
    auto changed_inclusion             = inclusion.value().value();
    changed_inclusion.transaction_hash = "network-transaction-changed";
    check("light client rejects a changed transaction proof",
          light_client.has_value() && !light_client.value().verify_transaction_proof(changed_inclusion));

    const auto epoch_inclusion = engines.front()->transaction_inclusion_proof(epoch_action_hash);
    check("finalized epoch action has an inclusion proof",
          epoch_inclusion.has_value() && epoch_inclusion.value().has_value());
    const auto epoch_scheduled = light_client.value().schedule_epoch(epoch_change,
                                                                     next_committee.document,
                                                                     governance_policy.value(),
                                                                     epoch_inclusion.value().value(),
                                                                     20);
    check("light client accepts the governed two-step epoch", epoch_scheduled.has_value());
    const auto light_client_state_path = root / "light-client.msgpack";
    check("light client writes its pending trust chain atomically",
          light_client.value().save(light_client_state_path).has_value());
    auto restored_light_client = LightClientVerifier::load(light_client_state_path);
    check("light client restores its pending trust chain",
          restored_light_client.has_value() && restored_light_client.value().pending_validators().has_value()
              && restored_light_client.value().pending_validators().value().document().epoch == 3);
    if (restored_light_client.has_value()) {
        light_client = std::move(restored_light_client);
    }
    const auto bootstrap =
        make_epoch_bootstrap(epoch_change, next_committee.document, epoch_inclusion.value().value());
    check("epoch handover binds the old decision certificate", bootstrap.has_value());
    const auto next_view = ValidatorSetView::create_epoch_transition(next_committee.document,
                                                                     epoch_change,
                                                                     governance_policy.value(),
                                                                     10,
                                                                     20);
    check("next committee is valid for the governed handover", next_view.has_value());

    std::vector<std::unique_ptr<ConsensusEngine>> next_engines;
    for (std::size_t index = 0; index < next_committee.keys.size(); ++index) {
        auto engine =
            std::make_unique<ConsensusEngine>(next_view.value(),
                                              ValidatorIdentity {
                                                  .validator_id =
                                                      validator_id_for(next_committee.keys[index].public_key()),
                                                  .key = next_committee.keys[index],
                                              },
                                              std::make_unique<SafetyStore>(
                                                  root
                                                  / ("epoch-3-validator-" + std::to_string(index) + ".sqlite")),
                                              ConsensusEngine::ProposalValidator {},
                                              bootstrap.value());
        check("next-epoch validator starts at the global handover height", engine->initialize().has_value());
        next_engines.push_back(std::move(engine));
    }
    check("new epoch genesis continues the global height",
          std::ranges::all_of(next_engines, [](const auto& engine) {
              return engine->genesis_certificate().height == 12;
          }));

    auto next_first    = bootstrap.value().first_dag_section;
    auto previous_root = bootstrap.value().previous_section_root;
    for (std::uint64_t height = 13; height <= 15; ++height) {
        const auto& leader       = next_view.value().leader(height, 0);
        const auto  leader_index = next_committee.index_for(leader.validator_id);
        const auto  proposal =
            next_engines[leader_index]->make_proposal(epoch_batch_manifest(height,
                                                                           next_first,
                                                                           previous_root,
                                                                           height == 13 ? third_epoch_action_hash
                                                                                        : std::string {}),
                                                      state_commitment(*next_engines[leader_index],
                                                                       height,
                                                                       "epoch-root-" + std::to_string(height)));
        check("new epoch proposes on the global chain", proposal.has_value());
        const auto                       data = batch_data(proposal.value());
        std::optional<QuorumCertificate> certificate;
        for (std::size_t index = 0; index < next_engines.size(); ++index) {
            if (index != leader_index) {
                check("new epoch observes the handover proposal",
                      next_engines[index]->observe_proposal(proposal.value()).has_value());
            }
            check("new epoch stages handover data", next_engines[index]->stage_batch(data).has_value());
            const auto vote = next_engines[index]->accept_proposal(proposal.value());
            check("new epoch validator votes", vote.has_value());
            if (vote.has_value()) {
                const auto accepted = next_engines[leader_index]->accept_vote(vote.value());
                if (accepted.has_value() && accepted.value().certificate.has_value()) {
                    certificate = accepted.value().certificate;
                }
            }
        }
        check("new epoch forms a five-of-seven certificate", certificate.has_value());
        for (auto& engine : next_engines) {
            check("new epoch accepts its certificate",
                  engine->accept_certificate(certificate.value()).has_value());
        }
        next_first += ShadowSectionInterval;
        previous_root = "epoch-root-" + std::to_string(height);
    }

    const auto new_epoch_inclusion = next_engines.front()->transaction_inclusion_proof("epoch-transaction-13-a");
    check("new epoch creates a finality proof",
          new_epoch_inclusion.has_value() && new_epoch_inclusion.value().has_value());
    check("light client verifies and activates the new epoch",
          new_epoch_inclusion.has_value() && new_epoch_inclusion.value().has_value()
              && light_client.value().advance(new_epoch_inclusion.value().value().finality_proof).has_value()
              && light_client.value().active_validators().document().epoch == 3);
    check("light client rejects a finalized proof below its trusted height",
          inclusion.has_value() && inclusion.value().has_value()
              && !light_client.value().advance(inclusion.value().value().finality_proof).has_value());
    check("light client saves the promoted trust chain",
          light_client.value().save(light_client_state_path).has_value());
    const auto promoted_light_client = LightClientVerifier::load(light_client_state_path);
    check("light client restores the promoted epoch and trusted height",
          promoted_light_client.has_value()
              && promoted_light_client.value().active_validators().document().epoch == 3
              && promoted_light_client.value().trusted_height() == 13);
    const auto third_epoch_inclusion = next_engines.front()->transaction_inclusion_proof(third_epoch_action_hash);
    check("second governed epoch action has an inclusion proof",
          third_epoch_inclusion.has_value() && third_epoch_inclusion.value().has_value());
    check("light client schedules a second validator transition",
          third_epoch_inclusion.has_value() && third_epoch_inclusion.value().has_value()
              && light_client.value()
                     .schedule_epoch(third_epoch_change,
                                     third_committee.document,
                                     governance_policy.value(),
                                     third_epoch_inclusion.value().value(),
                                     21)
                     .has_value());
    check("light client persists trust across two validator transitions",
          light_client.value().save(light_client_state_path).has_value()
              && LightClientVerifier::load(light_client_state_path).has_value());

    const auto third_bootstrap =
        make_epoch_bootstrap(third_epoch_change, third_committee.document, third_epoch_inclusion.value().value());
    const auto third_view = ValidatorSetView::create_epoch_transition(third_committee.document,
                                                                      third_epoch_change,
                                                                      governance_policy.value(),
                                                                      13,
                                                                      21);
    check("second handover has a valid bootstrap and validator set",
          third_bootstrap.has_value() && third_view.has_value());
    std::vector<std::unique_ptr<ConsensusEngine>> third_engines;
    for (std::size_t index = 0; index < third_committee.keys.size(); ++index) {
        auto engine =
            std::make_unique<ConsensusEngine>(third_view.value(),
                                              ValidatorIdentity {
                                                  .validator_id =
                                                      validator_id_for(third_committee.keys[index].public_key()),
                                                  .key = third_committee.keys[index],
                                              },
                                              std::make_unique<SafetyStore>(
                                                  root
                                                  / ("epoch-5-validator-" + std::to_string(index) + ".sqlite")),
                                              ConsensusEngine::ProposalValidator {},
                                              third_bootstrap.value());
        check("second handover validator starts at the global height", engine->initialize().has_value());
        third_engines.push_back(std::move(engine));
    }

    auto third_first         = third_bootstrap.value().first_dag_section;
    auto third_previous_root = third_bootstrap.value().previous_section_root;
    for (std::uint64_t height = 16; height <= 18; ++height) {
        const auto& leader       = third_view.value().leader(height, 0);
        const auto  leader_index = third_committee.index_for(leader.validator_id);
        const auto  proposal =
            third_engines[leader_index]->make_proposal(epoch_batch_manifest(height,
                                                                            third_first,
                                                                            third_previous_root),
                                                       state_commitment(*third_engines[leader_index],
                                                                        height,
                                                                        "epoch-5-root-" + std::to_string(height)));
        check("second handover proposes on the global chain", proposal.has_value());
        const auto                       data = batch_data(proposal.value());
        std::optional<QuorumCertificate> certificate;
        for (std::size_t index = 0; index < third_engines.size(); ++index) {
            if (index != leader_index) {
                check("second handover observes the proposal",
                      third_engines[index]->observe_proposal(proposal.value()).has_value());
            }
            check("second handover stages data", third_engines[index]->stage_batch(data).has_value());
            const auto vote = third_engines[index]->accept_proposal(proposal.value());
            check("second handover validator votes", vote.has_value());
            if (vote.has_value()) {
                const auto accepted = third_engines[leader_index]->accept_vote(vote.value());
                if (accepted.has_value() && accepted.value().certificate.has_value()) {
                    certificate = accepted.value().certificate;
                }
            }
        }
        check("second handover forms a five-of-seven certificate", certificate.has_value());
        for (auto& engine : third_engines) {
            check("second handover accepts its certificate",
                  engine->accept_certificate(certificate.value()).has_value());
        }
        third_first += ShadowSectionInterval;
        third_previous_root = "epoch-5-root-" + std::to_string(height);
    }

    const auto epoch_five_inclusion = third_engines.front()->transaction_inclusion_proof("epoch-transaction-16-a");
    check("second transition creates a finality proof",
          epoch_five_inclusion.has_value() && epoch_five_inclusion.value().has_value());
    check("light client activates the second validator transition",
          epoch_five_inclusion.has_value() && epoch_five_inclusion.value().has_value()
              && light_client.value().advance(epoch_five_inclusion.value().value().finality_proof).has_value()
              && light_client.value().active_validators().document().epoch == 5
              && light_client.value().trusted_height() == 16);
    const auto saved_epoch_five  = light_client.value().save(light_client_state_path);
    const auto loaded_epoch_five = LightClientVerifier::load(light_client_state_path);
    check("light client restores trust after two completed transitions",
          saved_epoch_five.has_value() && loaded_epoch_five.has_value()
              && loaded_epoch_five.value().active_validators().document().epoch == 5);

    third_engines.clear();
    next_engines.clear();
    engines.clear();
    std::filesystem::remove_all(root);
    std::printf("CONSENSUS NETWORK: %d pass, %d fail\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
