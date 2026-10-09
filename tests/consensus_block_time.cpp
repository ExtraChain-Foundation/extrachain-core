#include "chain/dag.h"
#include "consensus/consensus_service.h"
#include "core/extrachain_node.h"
#include "test_support.h"
#include "utils/exc_utils.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

namespace ExtraChain::Consensus {
    class ConsensusStateTestFixture {
    public:
        static void attach(ConsensusService& service, std::unique_ptr<ShadowConsensus> shadow) {
            service.consensus_ = std::move(shadow);
        }
        static auto build(ConsensusService&        service,
                          const SectionBatchData&  batch,
                          std::uint64_t            height,
                          const QuorumCertificate& parent) {
            return service.build_state_commitment(batch, "test-section-root", height, parent);
        }
    };
} // namespace ExtraChain::Consensus
using namespace ExtraChain::Consensus;

namespace {
    std::uint64_t now_ms() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count());
    }

    SectionBatchData empty_batch(std::uint64_t first, std::string previous_root) {
        SectionBatchData batch;
        batch.manifest.first_section         = first;
        batch.manifest.last_section          = first + ShadowSectionInterval - 1;
        batch.manifest.previous_section_root = std::move(previous_root);
        for (auto section = batch.manifest.first_section; section <= batch.manifest.last_section; ++section) {
            auto bytes = Json::serialize(Section { .id = SectionId(section) });
            batch.manifest.payload_bytes += bytes.size();
            batch.sections.emplace_back(section, std::move(bytes));
        }
        batch.manifest.transaction_root = calculate_transaction_root(batch.manifest.transaction_hashes);
        batch.manifest.data_root        = calculate_data_root(batch.sections);
        return batch;
    }
} // namespace

// Block time was the height number, so mining windows, emission and the date a wallet shows for a
// transaction all followed the block rate. The leader now sets the wall-clock time and every voter
// checks it against the parent and its own clock.
int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-block-time-" + Utils::generate_random_hex(12));
    TEST_REQUIRE(std::filesystem::create_directory(directory));
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    node->dag()->set_mode(DagMode::Full);
    Actor<KeyPrivate> network;
    network.create(ActorType::Service);
    std::vector<KeyPrivate>      keys(7);
    std::vector<ValidatorRecord> records;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        keys[index].generate_random();
        Actor<KeyPrivate> actor;
        actor.create(ActorType::Service);
        records.push_back(
            make_validator_record(network.id(), 1, actor, keys[index], "time-" + std::to_string(index), 0)
                .value());
    }
    const auto validators = make_validator_set(network.id(), 1, std::move(records), network).value();
    const auto view       = ValidatorSetView::create(validators).value();
    const auto key_of     = [&](const std::string& validator) {
        return *std::ranges::find_if(keys, [&](const auto& candidate) {
            return validator_id_for(candidate.public_key()) == validator;
        });
    };
    const auto leader = view.leader(1, 0).validator_id;
    TEST_REQUIRE(ShadowConsensus::write_validator_set(directory / "time-consensus", validators).has_value());
    TEST_REQUIRE(
        ShadowConsensus::write_identity(directory / "time-consensus",
                                        IdentityDocument { .validator_id = leader, .key = key_of(leader) })
            .has_value());
    auto shadow = ShadowConsensus::load(directory / "time-consensus", network.id());
    TEST_REQUIRE(shadow.has_value());
    auto&            engine  = shadow.value()->engine();
    const auto       genesis = engine.genesis_certificate();
    ConsensusService service(*node, directory / "time-consensus");
    ConsensusStateTestFixture::attach(service, std::move(shadow.value()));

    // The leader's proposal carries the current time.
    auto       first = empty_batch(1, "test-section-root");
    const auto state = ConsensusStateTestFixture::build(service, first, 1, genesis);
    TEST_REQUIRE(state.has_value());
    const auto before   = now_ms();
    const auto proposal = engine.make_proposal(first.manifest, state.value());
    const auto after    = now_ms();
    TEST_REQUIRE(proposal.has_value());
    TEST_REQUIRE(proposal.value().header.logical_time >= before && proposal.value().header.logical_time <= after);

    // A copy with another time, signed by the same leader, with its batch staged. A live copy
    // arrives the way a peer's proposal does, so this validator records when it first saw it.
    const auto variant = [&](const Proposal& base, std::uint64_t time, const SectionBatchData& data, bool live) {
        auto changed                = base;
        changed.header.logical_time = time;
        changed.signature = sign_payload(key_of(changed.proposer_id), proposal_signing_payload(changed)).value();
        if (live)
            TEST_REQUIRE(engine.observe_proposal(changed).has_value());
        else
            TEST_REQUIRE(engine.observe_certified_proposal(changed).has_value());
        auto staged        = data;
        staged.header_hash = hash_header(changed.header);
        TEST_REQUIRE(engine.stage_batch(staged).has_value());
        return changed;
    };
    const auto drift  = MaximumBlockClockDriftMs;
    const auto future = engine.accept_proposal(variant(proposal.value(), now_ms() + 2 * drift, first, true));
    TEST_REQUIRE(!future.has_value() && future.error() == ConsensusError::InvalidProposalTime);
    const auto past = engine.accept_proposal(variant(proposal.value(), now_ms() - 2 * drift, first, true));
    TEST_REQUIRE(!past.has_value() && past.error() == ConsensusError::InvalidProposalTime);
    const auto unseen = engine.accept_proposal(variant(proposal.value(), now_ms() - 2 * drift, first, false));
    TEST_REQUIRE(!unseen.has_value() && unseen.error() == ConsensusError::InvalidProposalTime);
    // Fresh when it arrived, but the vote is ready only after the drift has passed: fetching and
    // validating a batch under load takes that long, and refusing here would stall every round.
    const auto accepted = variant(proposal.value(), now_ms() - drift + 1500, first, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    const auto delayed = engine.accept_proposal(accepted);
    if (!delayed.has_value())
        std::fprintf(stderr,
                     "a proposal that arrived fresh was refused: %u\n",
                     static_cast<unsigned>(delayed.error()));
    TEST_REQUIRE(delayed.has_value());
    first.header_hash = hash_header(accepted.header);

    // Certify height 1, then propose height 2 from its leader.
    std::optional<QuorumCertificate> certificate;
    for (const auto& signer : keys) {
        Vote vote { .network_id   = network.id(),
                    .epoch        = 1,
                    .height       = 1,
                    .header_hash  = first.header_hash,
                    .validator_id = validator_id_for(signer.public_key()) };
        vote.signature      = sign_payload(signer, vote_signing_payload(vote)).value();
        const auto accepted = engine.accept_vote(vote);
        if (accepted.has_value() && accepted.value().certificate.has_value())
            certificate = accepted.value().certificate;
    }
    TEST_REQUIRE(certificate.has_value());
    TEST_REQUIRE(engine.accept_certificate(certificate.value()).has_value());
    auto second                 = empty_batch(ShadowSectionInterval + 1, accepted.header.section_root);
    second.manifest.parent_time = accepted.header.logical_time;
    const auto second_state = ConsensusStateTestFixture::build(service, second, 2, certificate.value());
    TEST_REQUIRE(second_state.has_value());
    auto next                           = accepted;
    next.header.height                  = 2;
    next.header.dag_section             = second.manifest.last_section;
    next.header.parent_certificate_hash = hash_certificate(certificate.value());
    next.header.batch_root              = hash_batch_manifest(second.manifest);
    next.header.transaction_root        = second.manifest.transaction_root;
    next.header.state_commitment        = hash_state_commitment(second_state.value());
    next.state                          = second_state.value();
    next.batch                          = second.manifest;
    next.parent_certificate             = certificate.value();
    next.proposer_id                    = view.leader(2, 0).validator_id;
    // Time must move forward from the parent block.
    const auto parent_time       = accepted.header.logical_time;
    auto       observed          = next;
    observed.header.logical_time = parent_time;
    observed.signature = sign_payload(key_of(observed.proposer_id), proposal_signing_payload(observed)).value();
    const auto seen    = engine.observe_proposal(observed);
    TEST_REQUIRE(!seen.has_value() && seen.error() == ConsensusError::InvalidProposalTime);
    const auto same = engine.accept_proposal(variant(next, parent_time, second, false));
    TEST_REQUIRE(!same.has_value() && same.error() == ConsensusError::InvalidProposalTime);
    // The batch carries its parent's time for the state it was built with; another value is refused.
    auto shifted                   = next;
    auto shifted_batch             = second;
    shifted_batch.manifest.parent_time += 1;
    shifted.batch                  = shifted_batch.manifest;
    shifted.header.batch_root      = hash_batch_manifest(shifted_batch.manifest);
    const auto wrong_parent        = engine.accept_proposal(
        variant(shifted, std::max(now_ms(), parent_time + 1), shifted_batch, false));
    TEST_REQUIRE(!wrong_parent.has_value() && wrong_parent.error() == ConsensusError::InvalidProposalTime);
    const auto later = engine.accept_proposal(variant(next, std::max(now_ms(), parent_time + 1), second, true));
    if (!later.has_value())
        std::fprintf(stderr, "a later block time was refused: %u\n", static_cast<unsigned>(later.error()));
    TEST_REQUIRE(later.has_value());

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
