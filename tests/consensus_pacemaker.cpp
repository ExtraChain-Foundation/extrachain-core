#include "chain/dag.h"
#include "consensus/consensus_service.h"
#include "core/extrachain_node.h"
#include "runtime/deadline_task.h"
#include "test_support.h"
#include "utils/exc_utils.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <atomic>
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
        static void arm(ConsensusService& service, std::shared_ptr<Core::DeadlineTask> timer) {
            std::lock_guard lock(service.mutex_);
            service.timeout_task_   = std::move(timer);
            service.voting_enabled_ = true;
        }
        static void disarm(ConsensusService& service) {
            std::lock_guard lock(service.mutex_);
            if (service.timeout_task_)
                service.timeout_task_->cancel();
            service.timeout_task_.reset();
            service.voting_enabled_ = false;
        }
        static bool apply(ConsensusService& service, const TimeoutCertificate& certificate) {
            std::lock_guard lock(service.mutex_);
            return service.apply_timeout_certificate(certificate);
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
using namespace std::chrono_literals;

// The stand stalled at height 932 round 2: validators that had not timed the round out kept
// receiving the round-1 timeout certificate again from the others, and every copy restarted
// their 16-second round timer, so it never fired and they were resent the certificate again.
int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-pacemaker-" + Utils::generate_random_hex(12));
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
            make_validator_record(network.id(), 1, actor, keys[index], "pacemaker-" + std::to_string(index), 0)
                .value());
    }
    const auto validators = make_validator_set(network.id(), 1, std::move(records), network).value();
    const auto view       = ValidatorSetView::create(validators).value();
    const auto leader     = view.leader(1, 0).validator_id;
    const auto key        = std::ranges::find_if(keys, [&](const auto& candidate) {
        return validator_id_for(candidate.public_key()) == leader;
    });
    TEST_REQUIRE(key != keys.end());
    TEST_REQUIRE(ShadowConsensus::write_validator_set(directory / "pacemaker-consensus", validators).has_value());
    TEST_REQUIRE(ShadowConsensus::write_identity(directory / "pacemaker-consensus",
                                                 IdentityDocument { .validator_id = leader, .key = *key })
                     .has_value());
    auto shadow = ShadowConsensus::load(directory / "pacemaker-consensus", network.id());
    TEST_REQUIRE(shadow.has_value());
    auto&            engine  = shadow.value()->engine();
    const auto       genesis = engine.genesis_certificate();
    ConsensusService service(*node, directory / "pacemaker-consensus");
    ConsensusStateTestFixture::attach(service, std::move(shadow.value()));

    // Certify height 1.
    SectionBatchData first;
    first.manifest.first_section         = 1;
    first.manifest.last_section          = ShadowSectionInterval;
    first.manifest.previous_section_root = "test-section-root";
    for (auto section = first.manifest.first_section; section <= first.manifest.last_section; ++section) {
        auto bytes = Json::serialize(Section { .id = SectionId(section) });
        first.manifest.payload_bytes += bytes.size();
        first.sections.emplace_back(section, std::move(bytes));
    }
    first.manifest.transaction_root = calculate_transaction_root(first.manifest.transaction_hashes);
    first.manifest.data_root        = calculate_data_root(first.sections);
    const auto state                = ConsensusStateTestFixture::build(service, first, 1, genesis);
    TEST_REQUIRE(state.has_value());
    const auto proposal = engine.make_proposal(first.manifest, state.value());
    TEST_REQUIRE(proposal.has_value());
    first.header_hash = hash_header(proposal.value().header);
    TEST_REQUIRE(engine.stage_batch(first).has_value());
    std::optional<QuorumCertificate> certificate;
    for (const auto& signer : keys) {
        Vote vote { .network_id   = network.id(),
                    .epoch        = 1,
                    .height       = 1,
                    .header_hash  = first.header_hash,
                    .validator_id = validator_id_for(signer.public_key()) };
        vote.signature = sign_payload(signer, vote_signing_payload(vote)).value();
        const auto accepted = engine.accept_vote(vote);
        TEST_REQUIRE(accepted.has_value());
        if (accepted.value().certificate.has_value())
            certificate = accepted.value().certificate;
    }
    TEST_REQUIRE(certificate.has_value());
    TEST_REQUIRE(engine.accept_certificate(certificate.value()).has_value());

    // A quorum times height 2 round 0 out.
    std::optional<TimeoutCertificate> timeout;
    for (const auto& signer : keys) {
        TimeoutVote vote { .network_id               = network.id(),
                           .epoch                    = 1,
                           .height                   = 2,
                           .round                    = 0,
                           .highest_certificate_hash = hash_certificate(certificate.value()),
                           .validator_id             = validator_id_for(signer.public_key()) };
        vote.signature      = sign_payload(signer, timeout_vote_signing_payload(vote)).value();
        const auto accepted = engine.accept_timeout_vote(vote);
        TEST_REQUIRE(accepted.has_value());
        if (accepted.value().certificate.has_value() && !timeout.has_value())
            timeout = accepted.value().certificate;
    }
    TEST_REQUIRE(timeout.has_value());

    boost::asio::io_context io;
    auto                    guard = boost::asio::make_work_guard(io);
    std::thread             runner([&] {
        io.run();
    });
    std::atomic_int fired { 0 };
    ConsensusStateTestFixture::arm(service, ExtraChain::Core::DeadlineTask::create(io.get_executor(), [&] {
                                       fired.fetch_add(1);
                                   }));
    TEST_REQUIRE(ConsensusStateTestFixture::apply(service, timeout.value()));
    TEST_REQUIRE_EQ(engine.safety_state().current_round, std::uint64_t(1));
    // Round 1 times out after 8 s. Copies of the same certificate keep arriving every second.
    const auto deadline = std::chrono::steady_clock::now() + 12s;
    while (fired.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1s);
        static_cast<void>(ConsensusStateTestFixture::apply(service, timeout.value()));
    }
    std::printf("round timer fired %d time(s) while the certificate was resent\n", fired.load());
    std::fflush(stdout);
    const auto timed_out = fired.load() > 0;

    ConsensusStateTestFixture::disarm(service);
    guard.reset();
    io.stop();
    runner.join();
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    TEST_REQUIRE(timed_out);
}
