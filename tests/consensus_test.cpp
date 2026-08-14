#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "consensus/consensus_engine.h"
#include "consensus/peer_authenticator.h"
#include "consensus/shadow_consensus.h"
#include "utils/exc_utils.h"

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

    std::optional<FinalizedCheckpoint> finalized;
    Proposal                           last_proposal;
    for (std::uint64_t height = 1; height <= 3; ++height) {
        const auto& leader_record = fixture.view.leader(height, 0);
        const auto  leader_index  = identity_index(fixture, leader_record.validator_id);
        auto        proposal      = engines[leader_index]->make_proposal(height * 20,
                                                             "section-root-" + std::to_string(height),
                                                             "transaction-root-" + std::to_string(height));
        check("scheduled leader creates proposal", proposal.has_value());
        if (!proposal.has_value()) {
            break;
        }
        last_proposal = proposal.value();

        std::optional<QuorumCertificate> certificate;
        for (auto& engine : engines) {
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
    check("finalized checkpoint keeps the DAG section",
          finalized.has_value() && finalized.value().dag_section == 20);
    if (!finalized.has_value()) {
        engines.clear();
        std::filesystem::remove_all(root);
        std::printf("CONSENSUS: %d pass, %d fail\n", passed, failed);
        return 1;
    }

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
                                  + conflicting_proposal.header.transaction_root + fixture.view.hash(),
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

    restarted.reset();
    engines.clear();
    std::filesystem::remove_all(root);
    std::printf("CONSENSUS: %d pass, %d fail\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
