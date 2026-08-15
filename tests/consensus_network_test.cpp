#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "consensus/consensus_engine.h"
#include "consensus/light_client.h"
#include "consensus/validator_set.h"
#include "utils/exc_utils.h"

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

    bool quorum_guard_checked = false;
    for (std::uint64_t height = 1; height <= 12; ++height) {
        const auto& leader       = committee.view.leader(height, 0);
        const auto  leader_index = committee.index_for(leader.validator_id);
        const auto  proposal =
            engines[leader_index]->make_proposal(batch_manifest(height,
                                                                height == 10 ? epoch_action_hash : std::string {}),
                                                 "network-root-" + std::to_string(height));
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
                                                      "epoch-root-" + std::to_string(height));
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
        const auto  proposal     = third_engines[leader_index]->make_proposal(epoch_batch_manifest(height,
                                                                                              third_first,
                                                                                              third_previous_root),
                                                                         "epoch-5-root-" + std::to_string(height));
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
