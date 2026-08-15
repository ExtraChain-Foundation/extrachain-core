#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "consensus/shadow_consensus.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"
#include "utils/file_io.h"

namespace {
    using namespace ExtraChain::Consensus;

    struct Committee {
        Actor<KeyPrivate>              network;
        std::vector<Actor<KeyPrivate>> actors;
        std::vector<KeyPrivate>        keys;
        ValidatorSet                   document;
        ValidatorSetView               view;

        Committee(Actor<KeyPrivate> network_actor = {}, std::uint64_t epoch = 1)
            : network(std::move(network_actor))
            , view(make_view(epoch)) {
        }

        ValidatorSetView make_view(std::uint64_t epoch) {
            if (network.empty()) {
                network.create(ActorType::Service);
            }
            std::vector<ValidatorRecord> records;
            for (std::size_t index = 0; index < ShadowCommitteeSize; ++index) {
                Actor<KeyPrivate> actor;
                actor.create(ActorType::Service);
                KeyPrivate key;
                key.generate_random();
                auto record = make_validator_record(network.id(),
                                                    epoch,
                                                    actor,
                                                    key,
                                                    "recovery-validator-" + std::to_string(index),
                                                    0);
                if (!record.has_value()) {
                    throw std::runtime_error("cannot create validator record");
                }
                actors.push_back(actor);
                keys.push_back(key);
                records.push_back(record.value());
            }
            auto signed_set = make_validator_set(network.id(), epoch, std::move(records), network);
            if (!signed_set.has_value()) {
                throw std::runtime_error("cannot sign validator set");
            }
            document    = signed_set.value();
            auto result = ValidatorSetView::create(document);
            if (!result.has_value()) {
                throw std::runtime_error("cannot create validator view");
            }
            return result.value();
        }

        std::size_t index_for(std::string_view validator_id) const {
            for (std::size_t index = 0; index < keys.size(); ++index) {
                if (validator_id_for(keys[index].public_key()) == validator_id) {
                    return index;
                }
            }
            throw std::runtime_error("cannot find validator identity");
        }
    };

    std::vector<std::string> public_keys(const std::vector<KeyPrivate>& keys) {
        std::vector<std::string> result;
        result.reserve(keys.size());
        for (const auto& key : keys) {
            result.push_back(Utils::to_base64(key.public_key()));
        }
        return result;
    }

    std::vector<OperatorAttestation> operator_attestations(const Committee& committee) {
        std::vector<OperatorAttestation> result;
        result.reserve(committee.actors.size());
        for (std::size_t index = 0; index < committee.actors.size(); ++index) {
            result.push_back(OperatorAttestation {
                .operator_id_hash     = Utils::calculate_hash("recovery-operator-" + std::to_string(index)),
                .actor_id             = committee.actors[index].id(),
                .node_identifier      = "recovery-validator-" + std::to_string(index),
                .consensus_public_key = Utils::to_base64(committee.keys[index].public_key()),
                .document_hash        = Utils::calculate_hash("recovery-attestation-" + std::to_string(index)),
            });
        }
        return result;
    }

    SectionBatchData batch_data(const Proposal& proposal) {
        std::vector<std::pair<std::uint64_t, std::string>> sections;
        for (auto section = proposal.batch.first_section; section <= proposal.batch.last_section; ++section) {
            sections.emplace_back(section,
                                  "recovery-section-" + std::to_string(proposal.header.height) + '-'
                                      + std::to_string(section));
        }
        return SectionBatchData {
            .header_hash = hash_header(proposal.header),
            .manifest    = proposal.batch,
            .sections    = std::move(sections),
        };
    }

    SectionBatchManifest batch_manifest(std::uint64_t height) {
        const auto                                         first = (height - 1) * ShadowSectionInterval + 1;
        std::vector<std::pair<std::uint64_t, std::string>> sections;
        std::uint64_t                                      payload_bytes = 0;
        for (auto section = first; section < first + ShadowSectionInterval; ++section) {
            auto bytes = "recovery-section-" + std::to_string(height) + '-' + std::to_string(section);
            payload_bytes += bytes.size();
            sections.emplace_back(section, std::move(bytes));
        }
        const std::vector<std::string> transaction_hashes { "recovery-transaction-" + std::to_string(height) };
        return SectionBatchManifest {
            .first_section      = first,
            .last_section       = first + ShadowSectionInterval - 1,
            .transaction_hashes = transaction_hashes,
            .transaction_root   = calculate_transaction_root(transaction_hashes),
            .data_root          = calculate_data_root(sections),
            .previous_section_root =
                height == 1 ? "activation-root" : "recovery-root-" + std::to_string(height - 1),
            .payload_bytes = payload_bytes,
        };
    }

    StateCommitmentV2 state_commitment(ConsensusEngine& engine, std::uint64_t height) {
        std::string previous;
        const auto& parent = engine.safety_state().highest_certificate;
        if (parent.has_value() && parent.value().phase != Phase::Genesis) {
            previous = engine.proposal_for(parent.value().header_hash).value().header.state_commitment;
        }
        return StateCommitmentV2 {
            .network_id                = engine.validators().document().network_id,
            .epoch                     = engine.validators().document().epoch,
            .height                    = height,
            .previous_state_commitment = std::move(previous),
            .section_root              = "recovery-root-" + std::to_string(height),
            .account_state_root        = "recovery-accounts-" + std::to_string(height),
            .contract_state_root       = "recovery-contracts-" + std::to_string(height),
            .token_registry_root       = "recovery-tokens-" + std::to_string(height),
            .validator_set_hash        = engine.validators().hash(),
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

    const auto root =
        std::filesystem::temp_directory_path() / ("extrachain-shadow-recovery-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(root);
    try {
        Committee               current;
        Committee               next(current.network, 2);
        std::vector<KeyPrivate> governance_keys(5);
        std::vector<KeyPrivate> recovery_keys(5);
        for (auto& key : governance_keys) {
            key.generate_random();
        }
        for (auto& key : recovery_keys) {
            key.generate_random();
        }
        const auto governance =
            make_multisig_policy(current.network.id(), GovernanceThreshold, public_keys(governance_keys));
        const auto recovery =
            make_multisig_policy(current.network.id(), RecoveryThreshold, public_keys(recovery_keys));
        check("governance policies are valid", governance.has_value() && recovery.has_value());

        TrustAnchorV1 anchor {
            .network_id         = current.network.id(),
            .initial_validators = current.document,
            .governance_policy  = governance.value(),
            .recovery_policy    = recovery.value(),
        };
        anchor.authorization = authorize_action(governance.value(),
                                                1,
                                                trust_anchor_action_hash(anchor),
                                                { governance_keys[0], governance_keys[1], governance_keys[2] })
                                   .value();
        ActivationManifestV1 activation {
            .network_id             = current.network.id(),
            .activation_height      = 1,
            .activation_dag_section = ShadowSectionInterval,
            .validator_set_hash     = current.view.hash(),
        };
        activation.authorization = authorize_action(governance.value(),
                                                    1,
                                                    activation_action_hash(activation),
                                                    { governance_keys[0], governance_keys[1], governance_keys[2] })
                                       .value();
        const auto configured =
            ShadowConsensus::write_configuration(root,
                                                 ShadowConfiguration {
                                                     .mode                   = ShadowMode::Finality,
                                                     .activation_height      = 1,
                                                     .activation_dag_section = ShadowSectionInterval,
                                                 })
                .has_value()
            && ShadowConsensus::write_validator_set(root, current.document).has_value()
            && ShadowConsensus::write_identity(root,
                                               IdentityDocument {
                                                   .validator_id =
                                                       validator_id_for(current.keys.front().public_key()),
                                                   .key = current.keys.front(),
                                               })
                   .has_value()
            && ShadowConsensus::write_governance_policy(root, governance.value()).has_value()
            && ShadowConsensus::write_recovery_policy(root, recovery.value()).has_value()
            && ShadowConsensus::write_trust_anchor(root, anchor).has_value()
            && ShadowConsensus::write_activation_manifest(root, activation, governance.value()).has_value();
        check("finality node configuration is durable", configured);
        auto shadow = ShadowConsensus::load(root, current.network.id());
        check("finality node loads before recovery", shadow.has_value());

        std::vector<std::unique_ptr<ConsensusEngine>> peers;
        for (std::size_t index = 1; index < current.keys.size(); ++index) {
            auto engine =
                std::make_unique<ConsensusEngine>(current.view,
                                                  ValidatorIdentity {
                                                      .validator_id =
                                                          validator_id_for(current.keys[index].public_key()),
                                                      .key = current.keys[index],
                                                  },
                                                  std::make_unique<SafetyStore>(
                                                      root / ("peer-" + std::to_string(index) + ".sqlite")));
            check("recovery peer initializes", engine->initialize().has_value());
            peers.push_back(std::move(engine));
        }
        const auto engine_at = [&](std::size_t index) -> ConsensusEngine& {
            return index == 0 ? shadow.value()->engine() : *peers[index - 1];
        };

        for (std::uint64_t height = 1; height <= 3; ++height) {
            const auto leader_index = current.index_for(current.view.leader(height, 0).validator_id);
            auto       proposal =
                engine_at(leader_index)
                    .make_proposal(batch_manifest(height), state_commitment(engine_at(leader_index), height));
            check("recovery baseline proposal is created", proposal.has_value());
            const auto                       data = batch_data(proposal.value());
            std::optional<QuorumCertificate> certificate;
            for (std::size_t index = 0; index < current.keys.size(); ++index) {
                auto& engine = engine_at(index);
                if (index != leader_index) {
                    check("recovery peer observes proposal",
                          engine.observe_proposal(proposal.value()).has_value());
                }
                check("recovery peer stages batch", engine.stage_batch(data).has_value());
                const auto vote = engine.accept_proposal(proposal.value());
                check("recovery peer votes", vote.has_value());
                if (vote.has_value()) {
                    const auto accepted = engine_at(leader_index).accept_vote(vote.value());
                    if (accepted.has_value() && accepted.value().certificate.has_value()) {
                        certificate = accepted.value().certificate;
                    }
                }
            }
            check("recovery baseline forms quorum", certificate.has_value());
            for (std::size_t index = 0; index < current.keys.size(); ++index) {
                check("recovery peer accepts certificate",
                      engine_at(index).accept_certificate(certificate.value()).has_value());
            }
        }

        const auto proof = shadow.value()->engine().finality_proofs_after(0, 1);
        check("recovery uses a finalized checkpoint", proof.has_value() && proof.value().size() == 1);
        const auto& finalized          = proof.value().front().finalized_proposal;
        auto        recovered_document = next.document;
        recovered_document.governance_public_key.clear();
        recovered_document.governance_signature.clear();
        RecoveryDocumentV2 decision {
            .network_id                 = current.network.id(),
            .recovery_sequence          = 1,
            .current_epoch              = 1,
            .activation_epoch           = 2,
            .finalized_height           = finalized.header.height,
            .finalized_header_hash      = hash_header(finalized.header),
            .finalized_state_commitment = finalized.header.state_commitment,
            .current_validator_set_hash = current.view.hash(),
            .next_validator_set_hash    = hash_validator_set(recovered_document),
            .registry_document_hash     = Utils::calculate_hash("recovery-registry"),
            .operators                  = operator_attestations(next),
            .signed_at_ms               = 1'000,
            .activate_after_ms          = 1'000 + MinimumRecoveryDelayMillis,
        };
        decision.authorization =
            authorize_action(recovery.value(),
                             1,
                             recovery_action_hash(decision),
                             { recovery_keys[0], recovery_keys[1], recovery_keys[2], recovery_keys[3] })
                .value();
        check("four recovery keys schedule the new committee",
              shadow.value()->schedule_recovery(decision, recovered_document, 1'000).has_value());
        shadow.value().reset();
        shadow = ShadowConsensus::load(root, current.network.id());
        check("restart keeps the old committee stopped while recovery is pending",
              shadow.has_value() && shadow.value()->pending_recovery().has_value()
                  && shadow.value()->engine().validators().document().epoch == 1);
        const auto early = shadow.value()->activate_scheduled_recovery(decision.activate_after_ms - 1);
        check("recovery cannot activate before the signed delay", early.has_value() && !early.value());
        const auto pending_bytes = FileIo::read_all(root / "pending-recovery.msgpack");
        check("pending recovery is durable before activation", pending_bytes.has_value());
        const auto activated = shadow.value()->activate_scheduled_recovery(decision.activate_after_ms);
        check("recovery activates after the delay",
              activated.has_value() && activated.value()
                  && shadow.value()->engine().validators().document().epoch == 2);
        shadow.value().reset();
        if (pending_bytes.has_value()) {
            check("simulated interrupted cleanup restores the pending marker",
                  FileIo::write_atomic(root / "pending-recovery.msgpack", pending_bytes.value()).has_value());
        }
        const auto restarted = ShadowConsensus::load(root, current.network.id());
        check("restarted node replays the unified recovery history",
              restarted.has_value() && restarted.value()->engine().validators().document().epoch == 2
                  && restarted.value()->epoch_starts().size() == 1
                  && !restarted.value()->pending_recovery().has_value()
                  && !std::filesystem::exists(root / "pending-recovery.msgpack"));
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "FAIL: %s\n", exception.what());
        ++failed;
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::printf("SHADOW RECOVERY: %d pass, %d fail\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
