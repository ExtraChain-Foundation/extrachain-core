#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "consensus/consensus_engine.h"
#include "consensus/peer_authenticator.h"
#include "consensus/shadow_consensus.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"

namespace {
    using namespace ExtraChain::Consensus;

    struct Fixture {
        std::size_t                    validator_count;
        Actor<KeyPrivate>              governance;
        std::vector<Actor<KeyPrivate>> actors;
        std::vector<KeyPrivate>        keys;
        ValidatorSet                   validator_set;
        ValidatorSetView               view;

        explicit Fixture(std::size_t count = 4)
            : validator_count(count)
            , view(make_view()) {
        }

    private:
        ValidatorSetView make_view() {
            governance.create(ActorType::Service);
            for (std::size_t index = 0; index < validator_count; ++index) {
                Actor<KeyPrivate> actor;
                actor.create(ActorType::Service);
                KeyPrivate key;
                key.generate_random();
                const auto record =
                    make_validator_record(governance.id(), 1, actor, key, "validator-" + std::to_string(index), 0);
                if (!record.has_value()) {
                    throw std::runtime_error("cannot create validator record");
                }
                actors.push_back(actor);
                keys.push_back(key);
                validator_set.validators.push_back(record.value());
            }
            const auto signed_set = make_validator_set(governance.id(), 1, validator_set.validators, governance);
            if (!signed_set.has_value()) {
                throw std::runtime_error("cannot create validator set");
            }
            validator_set      = signed_set.value();
            const auto created = ValidatorSetView::create(validator_set);
            if (!created.has_value()) {
                throw std::runtime_error("cannot validate validator set");
            }
            return created.value();
        }
    };

    std::size_t identity_index(const Fixture& fixture, std::string_view validator_id) {
        for (std::size_t index = 0; index < fixture.keys.size(); ++index) {
            if (validator_id_for(fixture.keys[index].public_key()) == validator_id) {
                return index;
            }
        }
        throw std::runtime_error("validator identity is absent");
    }

    std::vector<std::pair<std::uint64_t, std::string>> batch_sections(std::uint64_t height) {
        std::vector<std::pair<std::uint64_t, std::string>> sections;
        const auto                                         first = (height - 1) * 20 + 1;
        for (std::uint64_t section = first; section <= height * 20; ++section) {
            sections.emplace_back(section,
                                  "section-data-" + std::to_string(height) + '-' + std::to_string(section));
        }
        return sections;
    }

    SectionBatchManifest batch_manifest(std::uint64_t height) {
        const auto    transaction_hash = "transaction-" + std::to_string(height);
        const auto    sections         = batch_sections(height);
        std::uint64_t payload_bytes    = 0;
        for (const auto& [_, bytes] : sections) {
            payload_bytes += bytes.size();
        }
        return SectionBatchManifest {
            .first_section      = (height - 1) * 20 + 1,
            .last_section       = height * 20,
            .transaction_hashes = { transaction_hash },
            .transaction_root   = calculate_transaction_root({ transaction_hash }),
            .data_root          = calculate_data_root(sections),
            .previous_section_root =
                height == 1 ? "activation-root" : "section-root-" + std::to_string(height - 1),
            .payload_bytes = payload_bytes,
        };
    }

    SectionBatchData batch_data(std::uint64_t height, std::string header_hash) {
        return SectionBatchData {
            .header_hash = std::move(header_hash),
            .manifest    = batch_manifest(height),
            .sections    = batch_sections(height),
        };
    }

    StateCommitmentV2 state_commitment(ConsensusEngine& engine, std::uint64_t height, std::string section_root) {
        std::string previous;
        const auto& parent = engine.safety_state().highest_certificate;
        if (parent.has_value() && parent.value().phase != Phase::Genesis) {
            const auto proposal = engine.proposal_for(parent.value().header_hash);
            previous            = proposal.value().header.state_commitment;
        } else if (engine.epoch_bootstrap().has_value()) {
            previous = engine.epoch_bootstrap().value().previous_state_commitment;
        }
        return StateCommitmentV2 {
            .network_id                = engine.validators().document().network_id,
            .epoch                     = engine.validators().document().epoch,
            .height                    = height,
            .previous_state_commitment = std::move(previous),
            .section_root              = std::move(section_root),
            .account_state_root        = "account-root-" + std::to_string(height),
            .contract_state_root       = "contract-root-" + std::to_string(height),
            .token_registry_root       = "token-root-" + std::to_string(height),
            .validator_set_hash        = engine.validators().hash(),
        };
    }
} // namespace

int main() {
    int        passed = 0;
    int        failed = 0;
    const auto check  = [&](const char* name, bool result) {
        std::printf("  [%s] %s\n", result ? "PASS" : "FAIL", name);
        result ? ++passed : ++failed;
    };

    Fixture fixture;
    check("four-validator quorum is three", fixture.view.quorum() == 3);
    check("four-validator fault limit is one", fixture.view.fault_limit() == 1);
    for (const auto [size, quorum] :
         std::vector<std::pair<std::size_t, std::size_t>> { { 7, 5 }, { 10, 7 }, { 13, 9 } }) {
        Fixture boundary(size);
        check("larger validator set has the specified quorum", boundary.view.quorum() == quorum);
        check("validator set uses the one-third fault limit", boundary.view.fault_limit() == (size - 1) / 3);
    }

    auto reordered = fixture.validator_set;
    std::ranges::reverse(reordered.validators);
    const auto reordered_view = ValidatorSetView::create(reordered);
    check("validator-set signature is independent of record order", reordered_view.has_value());

    auto       duplicate_records = fixture.validator_set.validators;
    KeyPrivate duplicate_key;
    duplicate_key.generate_random();
    const auto duplicate_record =
        make_validator_record(fixture.governance.id(), 1, fixture.actors[0], duplicate_key, "duplicate-actor", 0);
    const auto original_actor =
        std::ranges::find(duplicate_records, fixture.actors[0].id(), &ValidatorRecord::actor_id);
    const auto replacement_index = original_actor == duplicate_records.begin() ? std::size_t(1) : std::size_t(0);
    duplicate_records[replacement_index] = duplicate_record.value();
    const auto duplicate_set =
        make_validator_set(fixture.governance.id(), 1, duplicate_records, fixture.governance);
    check("duplicate active actor is rejected",
          duplicate_set.has_value() && !ValidatorSetView::create(duplicate_set.value()).has_value());

    const auto        authenticated_index  = std::size_t(0);
    const auto        authenticated_id     = validator_id_for(fixture.keys[authenticated_index].public_key());
    const auto*       authenticated_record = fixture.view.find(authenticated_id);
    PeerAuthenticator challenger(fixture.view);
    PeerAuthenticator responder(fixture.view,
                                ValidatorIdentity { .validator_id = authenticated_id,
                                                    .key          = fixture.keys[authenticated_index] });
    const auto challenge = challenger.create_challenge("challenger-node", authenticated_record->node_identifier);
    const auto response  = responder.answer_challenge(challenge.value(), authenticated_record->node_identifier);
    const auto authentication =
        challenger.verify_response(response.value(), authenticated_record->node_identifier);
    check("challenge-response authenticates a validator connection",
          authentication.has_value() && authentication.value() == authenticated_id);
    check("authentication response cannot be replayed",
          !challenger.verify_response(response.value(), authenticated_record->node_identifier).has_value());
    challenger.forget_peer(authenticated_record->node_identifier);
    check("a reconnected peer must authenticate again",
          !challenger.is_authenticated(authenticated_record->node_identifier, authenticated_id));
    auto expired_challenge              = challenge.value();
    expired_challenge.expires_at_millis = 0;
    check("an expired challenge is not signed",
          !responder.answer_challenge(expired_challenge, authenticated_record->node_identifier).has_value());

    const auto root =
        std::filesystem::temp_directory_path() / ("extrachain-consensus-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(root);

    {
        const auto validator_directory = root / "configured-validator";
        const auto validator_id        = validator_id_for(fixture.keys.front().public_key());
        check("signed validator set is written atomically",
              ShadowConsensus::write_validator_set(validator_directory, fixture.validator_set).has_value());
        check("private consensus identity is written atomically",
              ShadowConsensus::write_identity(validator_directory,
                                              IdentityDocument { .validator_id = validator_id,
                                                                 .key          = fixture.keys.front() })
                  .has_value());
        const auto configured_validator = ShadowConsensus::load(validator_directory, fixture.governance.id());
        check("configured validator loads in voting mode",
              configured_validator.has_value() && configured_validator.value()->engine().identity().has_value());

        const auto observer_directory = root / "observer";
        check("observer receives the signed validator set",
              ShadowConsensus::write_validator_set(observer_directory, fixture.validator_set).has_value());
        const auto observer = ShadowConsensus::load(observer_directory, fixture.governance.id());
        check("observer loads without a private validator key",
              observer.has_value() && !observer.value()->engine().identity().has_value());
        Fixture    finality_fixture(7);
        const auto finality_directory = root / "finality-validator";
        check("finality configuration is written atomically",
              ShadowConsensus::write_configuration(finality_directory,
                                                   ShadowConfiguration { .mode              = ShadowMode::Finality,
                                                                         .activation_height = 10,
                                                                         .activation_dag_section = 200 })
                  .has_value());
        check("finality configuration requires an activation section",
              !ShadowConsensus::write_configuration(finality_directory,
                                                    ShadowConfiguration { .mode = ShadowMode::Finality,
                                                                          .activation_height      = 1,
                                                                          .activation_dag_section = 0 })
                   .has_value());
        check("finality receives the signed validator set",
              ShadowConsensus::write_validator_set(finality_directory, finality_fixture.validator_set)
                  .has_value());
        check("finality cannot load without a governed activation",
              !ShadowConsensus::load(finality_directory, finality_fixture.governance.id()).has_value());
        std::vector<KeyPrivate>  governance_keys(5);
        std::vector<std::string> governance_public_keys;
        for (auto& key : governance_keys) {
            key.generate_random();
            governance_public_keys.push_back(Utils::to_base64(key.public_key()));
        }
        const auto governance_policy =
            make_multisig_policy(finality_fixture.governance.id(), GovernanceThreshold, governance_public_keys);
        std::vector<KeyPrivate>  recovery_keys(5);
        std::vector<std::string> recovery_public_keys;
        for (auto& key : recovery_keys) {
            key.generate_random();
            recovery_public_keys.push_back(Utils::to_base64(key.public_key()));
        }
        const auto recovery_policy =
            make_multisig_policy(finality_fixture.governance.id(), RecoveryThreshold, recovery_public_keys);
        check("governance policy is written atomically",
              governance_policy.has_value()
                  && ShadowConsensus::write_governance_policy(finality_directory, governance_policy.value())
                         .has_value());
        check("recovery policy is written atomically",
              recovery_policy.has_value()
                  && ShadowConsensus::write_recovery_policy(finality_directory, recovery_policy.value())
                         .has_value());
        TrustAnchorV1 anchor {
            .network_id         = finality_fixture.governance.id(),
            .initial_validators = finality_fixture.validator_set,
            .governance_policy  = governance_policy.value(),
            .recovery_policy    = recovery_policy.value(),
        };
        anchor.authorization = authorize_action(governance_policy.value(),
                                                1,
                                                trust_anchor_action_hash(anchor),
                                                { governance_keys[0], governance_keys[1], governance_keys[2] })
                                   .value();
        check("trust anchor is written atomically",
              ShadowConsensus::write_trust_anchor(finality_directory, anchor).has_value());
        ActivationManifestV1 manifest {
            .network_id             = finality_fixture.governance.id(),
            .activation_height      = 10,
            .activation_dag_section = 200,
            .validator_set_hash     = finality_fixture.view.hash(),
        };
        const auto manifest_authorization =
            authorize_action(governance_policy.value(),
                             1,
                             activation_action_hash(manifest),
                             { governance_keys[0], governance_keys[1], governance_keys[2] });
        manifest.authorization = manifest_authorization.value();
        check("activation manifest is written atomically",
              ShadowConsensus::write_activation_manifest(finality_directory, manifest, governance_policy.value())
                  .has_value());
        const auto finality = ShadowConsensus::load(finality_directory, finality_fixture.governance.id());
        check("finality loads only after governed activation", finality.has_value());
    }

    std::vector<std::unique_ptr<ConsensusEngine>> engines;
    for (std::size_t index = 0; index < fixture.keys.size(); ++index) {
        auto engine =
            std::make_unique<ConsensusEngine>(fixture.view,
                                              ValidatorIdentity { .validator_id = validator_id_for(
                                                                      fixture.keys[index].public_key()),
                                                                  .key = fixture.keys[index] },
                                              std::make_unique<SafetyStore>(
                                                  root / ("validator-" + std::to_string(index) + ".sqlite")));
        check("validator engine initializes", engine->initialize().has_value());
        engines.push_back(std::move(engine));
    }

    std::optional<TimeoutCertificate> timeout_certificate;
    std::size_t                       timeout_vote_count = 0;
    for (auto& engine : engines) {
        const auto timeout_vote = engine->make_timeout_vote(1, 0);
        check("validator persists a timeout vote", timeout_vote.has_value());
        if (!timeout_vote.has_value()) {
            continue;
        }
        const auto accepted = engines.front()->accept_timeout_vote(timeout_vote.value());
        check("timeout vote is accepted", accepted.has_value());
        ++timeout_vote_count;
        if (timeout_vote_count == 2) {
            check("a two-validator partition cannot change the round",
                  accepted.has_value() && !accepted.value().certificate.has_value());
        }
        if (accepted.has_value() && accepted.value().certificate.has_value()) {
            timeout_certificate = accepted.value().certificate;
        }
    }
    check("timeout quorum forms a certificate", timeout_certificate.has_value());
    if (timeout_certificate.has_value()) {
        auto invalid_timeout_certificate = timeout_certificate.value();
        invalid_timeout_certificate.signatures.front()[0] =
            invalid_timeout_certificate.signatures.front()[0] == 'A' ? 'B' : 'A';
        check("timeout certificate with a changed signature is rejected",
              !engines.front()->verify_timeout_certificate(invalid_timeout_certificate));
        for (auto& engine : engines) {
            check("timeout certificate advances the round",
                  engine->accept_timeout_certificate(timeout_certificate.value()).has_value()
                      && engine->safety_state().current_round == 1);
        }
    }

    std::optional<FinalizedCheckpoint> finalized;
    Proposal                           last_proposal;
    std::vector<Proposal>              chain_proposals;
    std::vector<QuorumCertificate>     chain_certificates;
    for (std::uint64_t height = 1; height <= 3; ++height) {
        const auto  round         = height == 1 ? std::uint64_t(1) : std::uint64_t(0);
        const auto& leader_record = fixture.view.leader(height, round);
        const auto  leader_index  = identity_index(fixture, leader_record.validator_id);
        auto        proposal =
            engines[leader_index]->make_proposal(batch_manifest(height),
                                                 state_commitment(*engines[leader_index],
                                                                  height,
                                                                  "section-root-" + std::to_string(height)),
                                                 round);
        check("scheduled leader creates proposal", proposal.has_value());
        if (!proposal.has_value()) {
            break;
        }
        last_proposal = proposal.value();
        chain_proposals.push_back(proposal.value());

        const auto proposal_hash = hash_header(proposal.value().header);
        check("leader stores proposal data before voting",
              engines[leader_index]->stage_batch(batch_data(height, proposal_hash)).has_value());

        std::optional<QuorumCertificate> certificate;
        for (std::size_t index = 0; index < engines.size(); ++index) {
            auto& engine = engines[index];
            if (index != leader_index) {
                check("validator observes a proposal before data arrives",
                      engine->observe_proposal(proposal.value()).has_value());
                const auto unavailable_vote = engine->accept_proposal(proposal.value());
                check("validator refuses to vote before durable data",
                      !unavailable_vote.has_value()
                          && unavailable_vote.error() == ConsensusError::DataUnavailable);
                check("validator stores proposal data before voting",
                      engine->stage_batch(batch_data(height, proposal_hash)).has_value());
            }
            const auto vote = engine->accept_proposal(proposal.value());
            check("validator accepts safe proposal", vote.has_value());
            if (!vote.has_value()) {
                continue;
            }
            const auto accepted = engines[leader_index]->accept_vote(vote.value());
            check("leader accepts valid vote", accepted.has_value());
            if (accepted.has_value() && accepted.value().certificate.has_value()) {
                certificate = accepted.value().certificate;
            }
        }
        check("leader forms quorum certificate", certificate.has_value());
        if (!certificate.has_value()) {
            break;
        }
        chain_certificates.push_back(certificate.value());
        for (auto& engine : engines) {
            const auto result = engine->accept_certificate(certificate.value());
            check("validator accepts quorum certificate", result.has_value());
            if (result.has_value() && result.value().has_value()) {
                finalized = result.value();
            }
        }
    }
    check("three certified links finalize the grandparent",
          finalized.has_value() && finalized.value().height == 1);
    auto lagging_engine =
        std::make_unique<ConsensusEngine>(fixture.view,
                                          std::nullopt,
                                          std::make_unique<SafetyStore>(root / "lagging.sqlite"));
    check("data-lagging engine initializes", lagging_engine->initialize().has_value());
    bool lagging_rejected_finality = false;
    for (std::size_t index = 0; index < chain_proposals.size() && index < chain_certificates.size(); ++index) {
        check("data-lagging engine observes proposal",
              lagging_engine->observe_proposal(chain_proposals[index]).has_value());
        const auto accepted = lagging_engine->accept_certificate(chain_certificates[index]);
        if (index + 1 == chain_certificates.size()) {
            lagging_rejected_finality =
                !accepted.has_value() && accepted.error() == ConsensusError::DataUnavailable;
        } else {
            check("certificate before finality does not require batch data", accepted.has_value());
        }
    }
    check("finality waits for the finalized batch", lagging_rejected_finality);
    check("missing batch cannot advance finalized height", lagging_engine->safety_state().finalized_height == 0);
    lagging_engine.reset();

    const auto certified_observer_path = root / "certified-observer.sqlite";
    auto       certified_observer =
        std::make_unique<ConsensusEngine>(fixture.view,
                                          std::nullopt,
                                          std::make_unique<SafetyStore>(certified_observer_path));
    check("certified observer initializes", certified_observer->initialize().has_value());
    for (std::size_t index = 0; index < chain_proposals.size() && index < chain_certificates.size(); ++index) {
        check("certified observer sees proposal",
              certified_observer->observe_proposal(chain_proposals[index]).has_value());
        if (index == 0) {
            check("certified observer stores finalized batch",
                  certified_observer
                      ->stage_batch(batch_data(chain_proposals[index].header.height,
                                               hash_header(chain_proposals[index].header)))
                      .has_value());
        }
        check("certified observer accepts certificate without latest batch",
              certified_observer->accept_certificate(chain_certificates[index]).has_value());
    }
    certified_observer.reset();
    certified_observer = std::make_unique<ConsensusEngine>(fixture.view,
                                                           std::nullopt,
                                                           std::make_unique<SafetyStore>(certified_observer_path));
    check("certified observer restarts with referenced proposals",
          certified_observer->initialize().has_value()
              && certified_observer->proposal_for(hash_header(chain_proposals.back().header)).has_value());
    const auto observer_proofs = certified_observer->finality_proofs_after(0, 1);
    check("certified observer reloads its finality proof",
          observer_proofs.has_value() && observer_proofs.value().size() == 1);
    certified_observer.reset();

    check("finalized checkpoint keeps the DAG section",
          finalized.has_value() && finalized.value().dag_section == 20);
    const auto proofs = engines.front()->finality_proofs_after(0, 8);
    check("finality creates a portable three-chain proof",
          proofs.has_value() && proofs.value().size() == 1
              && engines.front()->verify_finality_proof(proofs.value().front()));
    const auto inclusion = engines.front()->transaction_inclusion_proof("transaction-1");
    check("finalized transaction has a Merkle inclusion proof",
          inclusion.has_value() && inclusion.value().has_value()
              && engines.front()->verify_transaction_inclusion_proof(inclusion.value().value()));
    if (inclusion.has_value() && inclusion.value().has_value()) {
        auto changed_inclusion             = inclusion.value().value();
        changed_inclusion.transaction_hash = "transaction-changed";
        check("changed transaction inclusion proof is rejected",
              !engines.front()->verify_transaction_inclusion_proof(changed_inclusion));
    } else {
        check("changed transaction inclusion proof is rejected", false);
    }
    if (proofs.has_value() && !proofs.value().empty()) {
        auto changed_proof                             = proofs.value().front();
        changed_proof.decision_certificate.header_hash = "changed-header";
        check("a changed finality proof is rejected", !engines.front()->verify_finality_proof(changed_proof));
    } else {
        check("a changed finality proof is rejected", false);
    }
    if (!finalized.has_value()) {
        engines.clear();
        std::filesystem::remove_all(root);
        std::printf("CONSENSUS: %d pass, %d fail\n", passed, failed);
        return 1;
    }
    check("the finalized batch remains available for repair",
          engines.front()->batch_for(finalized.value().header_hash).has_value());
    check("batch lookup treats a peer hash as data", !engines.front()->batch_for("' OR 1=1 --").has_value());

    const auto validator_path  = root / "durable-validator.sqlite";
    const auto validator_index = (identity_index(fixture, last_proposal.proposer_id) + 1) % fixture.keys.size();
    const auto validator_identity =
        ValidatorIdentity { .validator_id = validator_id_for(fixture.keys[validator_index].public_key()),
                            .key          = fixture.keys[validator_index] };
    auto restarted_validator = std::make_unique<ConsensusEngine>(fixture.view,
                                                                 validator_identity,
                                                                 std::make_unique<SafetyStore>(validator_path));
    check("fresh validator engine initializes", restarted_validator->initialize().has_value());
    const auto observed_hash = hash_header(last_proposal.header);
    check("validator accepts a proposal from another leader",
          restarted_validator->observe_proposal(last_proposal).has_value());
    check("validator durably stages data before a vote",
          restarted_validator->stage_batch(batch_data(last_proposal.header.height, observed_hash)).has_value());
    restarted_validator.reset();
    restarted_validator = std::make_unique<ConsensusEngine>(fixture.view,
                                                            validator_identity,
                                                            std::make_unique<SafetyStore>(validator_path));
    check("validator restarts with staged proposal data",
          restarted_validator->initialize().has_value()
              && restarted_validator->batch_for(observed_hash).has_value());
    restarted_validator.reset();

    const auto observer_path = root / "durable-observer.sqlite";
    auto       observer      = std::make_unique<ConsensusEngine>(fixture.view,
                                                      std::nullopt,
                                                      std::make_unique<SafetyStore>(observer_path));
    check("observer engine initializes", observer->initialize().has_value());
    check("observer accepts a valid proposal", observer->observe_proposal(last_proposal).has_value());
    check("observer stores proposal and batch atomically",
          observer->stage_batch(batch_data(last_proposal.header.height, observed_hash)).has_value());
    observer.reset();
    observer = std::make_unique<ConsensusEngine>(fixture.view,
                                                 std::nullopt,
                                                 std::make_unique<SafetyStore>(observer_path));
    check("observer restarts from durable proposal and batch",
          observer->initialize().has_value() && observer->batch_for(observed_hash).has_value());
    observer.reset();

    auto invalid_certificate                  = engines.front()->safety_state().highest_certificate.value();
    invalid_certificate.signatures.front()[0] = invalid_certificate.signatures.front()[0] == 'A' ? 'B' : 'A';
    check("certificate with a changed signature is rejected",
          !engines.front()->verify_certificate(invalid_certificate));
    const auto leader_index = identity_index(fixture, last_proposal.proposer_id);

    auto wrong_network_vote = Vote {
        .protocol_version = ProtocolVersion,
        .network_id       = fixture.actors.front().id(),
        .epoch            = fixture.view.document().epoch,
        .height           = last_proposal.header.height,
        .round            = last_proposal.header.round,
        .phase            = Phase::Prepare,
        .header_hash      = hash_header(last_proposal.header),
        .validator_id     = validator_id_for(fixture.keys.front().public_key()),
    };
    wrong_network_vote.signature =
        sign_payload(fixture.keys.front(), vote_signing_payload(wrong_network_vote)).value();
    check("vote for another network is rejected",
          !engines[leader_index]->accept_vote(wrong_network_vote).has_value());

    const auto replayed_vote = engines.front()->accept_proposal(last_proposal);
    check("an identical proposal replay is idempotent",
          replayed_vote.has_value() && replayed_vote.value().header_hash == hash_header(last_proposal.header));

    auto wrong_epoch_proposal = last_proposal;
    wrong_epoch_proposal.header.epoch += 1;
    wrong_epoch_proposal.signature =
        sign_payload(fixture.keys[leader_index], proposal_signing_payload(wrong_epoch_proposal)).value();
    check("proposal for another epoch is rejected",
          !engines.front()->accept_proposal(wrong_epoch_proposal).has_value());

    auto conflicting_proposal                = last_proposal;
    conflicting_proposal.header.section_root = "conflicting-section-root";
    conflicting_proposal.header.state_commitment =
        Utils::calculate_hash("EXC_CONSENSUS_STATE_V1" + hash_certificate(conflicting_proposal.parent_certificate)
                                  + conflicting_proposal.header.section_root
                                  + conflicting_proposal.header.batch_root + fixture.view.hash(),
                              Utils::HashAlgorithm::Blake3);
    conflicting_proposal.signature =
        sign_payload(fixture.keys[leader_index], proposal_signing_payload(conflicting_proposal)).value();
    check("conflicting proposal from the scheduled leader is rejected",
          !engines.front()->accept_proposal(conflicting_proposal).has_value());

    const auto voter_index = (leader_index + 1) % engines.size();
    Vote       first_vote {
              .protocol_version = ProtocolVersion,
              .network_id       = fixture.view.document().network_id,
              .epoch            = fixture.view.document().epoch,
              .height           = last_proposal.header.height + 1,
              .round            = 0,
              .phase            = Phase::Prepare,
              .header_hash      = "branch-a",
              .validator_id     = validator_id_for(fixture.keys[voter_index].public_key()),
    };
    first_vote.signature    = sign_payload(fixture.keys[voter_index], vote_signing_payload(first_vote)).value();
    auto second_vote        = first_vote;
    second_vote.header_hash = "branch-b";
    second_vote.signature   = sign_payload(fixture.keys[voter_index], vote_signing_payload(second_vote)).value();
    const auto first_observation  = engines[leader_index]->accept_vote(first_vote);
    const auto second_observation = engines[leader_index]->accept_vote(second_vote);
    check("unknown proposal vote is not accepted", !first_observation.has_value());
    check("two signed votes in one slot produce equivocation evidence",
          second_observation.has_value() && second_observation.value().equivocation.has_value());

    const auto restart_index = std::size_t(0);
    const auto restart_state = engines[restart_index]->safety_state();
    engines[restart_index].reset();
    auto restarted =
        std::make_unique<ConsensusEngine>(fixture.view,
                                          ValidatorIdentity { .validator_id = validator_id_for(
                                                                  fixture.keys[restart_index].public_key()),
                                                              .key = fixture.keys[restart_index] },
                                          std::make_unique<SafetyStore>(root / "validator-0.sqlite"));
    check("validator restarts from durable safety state", restarted->initialize().has_value());
    check("restart restores the last vote height",
          restarted->safety_state().last_voted_height == restart_state.last_voted_height);
    check("restart restores finality height",
          restarted->safety_state().finalized_height == restart_state.finalized_height);
    check("restart restores durable proposal data",
          restarted->batch_for(finalized.value().header_hash).has_value());
    const auto restarted_proofs = restarted->finality_proofs_after(0, 8);
    check("restart restores the finality proof index",
          restarted_proofs.has_value() && restarted_proofs.value().size() == 1
              && restarted->verify_finality_proof(restarted_proofs.value().front()));
    const auto restarted_timeout = restarted->make_timeout_vote(4, 0);
    check("restart restores the high certificate used by timeout votes",
          restarted_timeout.has_value() && restarted->accept_timeout_vote(restarted_timeout.value()).has_value());

    restarted.reset();
    engines.clear();
    std::filesystem::remove_all(root);
    std::printf("CONSENSUS: %d pass, %d fail\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
