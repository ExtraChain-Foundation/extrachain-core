#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include "chain/actor.h"
#include "chain/dag.h"
#include "chain/dag_recovery.h"
#include "consensus/consensus_protocol.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/responder.h"
#include "test_support.h"

namespace {
    class CapturingSender final : public ResponseSender {
    public:
        std::string send_response(const std::string &data_serialized,
                                  MessageType        type,
                                  SendMode,
                                  MessageStatus,
                                  const Responder &) override {
            payload      = data_serialized;
            message_type = type;
            ++responses;
            return {};
        }

        std::string payload;
        MessageType message_type = MessageType::Custom;
        std::size_t responses    = 0;
    };
} // namespace

int main() {
    const auto original_path = std::filesystem::current_path();
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-dag-recovery-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    Transaction transaction;
    transaction.set_sender(actor.id());
    transaction.set_receiver(actor.id());
    transaction.set_token(actor.id());
    transaction.set_type(TransactionType::Reward);
    transaction.set_amount(BigNumberFloat("0.01"));
    transaction.set_section(SectionId(42));
    TEST_REQUIRE(transaction.sign(actor));

    const auto recovery_path = test_path / "recovery" / "Recovery.db";
    {
        DagRecoveryJournal recovery(recovery_path);
        TEST_REQUIRE(recovery.available());
        const auto incident =
            recovery.record(SectionId(40), SectionId(50), "test-reason", "test-source", "observed-root");
        TEST_REQUIRE(incident.has_value());
        TEST_REQUIRE_EQ(recovery.pending_count(), std::uint64_t(1));
        TEST_REQUIRE(
            recovery.advance(incident.value(), DagRecoveryStage::Repairing, "expected-root", "proof-hash"));
        const auto repairing = recovery.pending_in_range(SectionId(45), SectionId(45));
        TEST_REQUIRE_EQ(repairing.size(), std::size_t(1));
        TEST_REQUIRE_EQ(repairing.front().stage, DagRecoveryStage::Repairing);
        TEST_REQUIRE_EQ(repairing.front().expected_root, std::string("expected-root"));
        TEST_REQUIRE(recovery.advance(incident.value(), DagRecoveryStage::Resolved));
        TEST_REQUIRE_EQ(recovery.pending_count(), std::uint64_t(0));

        const auto repeated =
            recovery.record(SectionId(40), SectionId(50), "test-reason", "test-source", "second-root");
        TEST_REQUIRE(repeated.has_value());
        TEST_REQUIRE(recovery.advance(repeated.value(), DagRecoveryStage::Failed));
        TEST_REQUIRE(recovery.record(SectionId(40), SectionId(50), "test-reason", "test-source").has_value());
        const auto failed = recovery.pending();
        TEST_REQUIRE_EQ(failed.size(), std::size_t(1));
        TEST_REQUIRE_EQ(failed.front().stage, DagRecoveryStage::Failed);
    }
    {
        DagRecoveryJournal recovered(recovery_path);
        const auto         failed = recovered.pending();
        TEST_REQUIRE_EQ(failed.size(), std::size_t(1));
        TEST_REQUIRE_EQ(failed.front().stage, DagRecoveryStage::Failed);
    }

    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    node->dag()->set_mode(DagMode::Full);
    node->account_controller()->create_profile("dag-recovery-profile", ActorType::User, actor);

    const auto make_reward = [&actor](const SectionId &section, std::uint64_t timestamp) {
        Transaction reward;
        reward.set_sender(actor.id());
        reward.set_receiver(actor.id());
        reward.set_token(actor.id());
        reward.set_type(TransactionType::Reward);
        reward.set_amount(BigNumberFloat("0.01"));
        reward.set_section(section);
        reward.set_timestamp(timestamp);
        TEST_REQUIRE(reward.sign(actor));
        return reward;
    };

    TEST_REQUIRE(node->dag()->save_transaction(make_reward(SectionId(1), 1)));
    TEST_REQUIRE(node->dag()->save_transaction(make_reward(SectionId(45), 2)));
    node->dag()->cache().write_cached_balances({}, SectionId(40));
    TEST_REQUIRE(node->dag()->generate_hash_from_section(SectionId(0), Force::Active, Force::None).has_value());
    const auto initial_control_20 = node->dag()->read_control(SectionId(20));
    const auto initial_control_40 = node->dag()->read_control(SectionId(40));
    TEST_REQUIRE(initial_control_20.has_value());
    TEST_REQUIRE(initial_control_40.has_value());
    const auto control_20_before = initial_control_20.value().control;
    const auto control_40_before = initial_control_40.value().control;

    node->dag()->report_state_inconsistency(SectionId(5), "negative-balance-transition", "dag-recovery-test");
    TEST_REQUIRE_EQ(node->dag()->state_projection().status, StateProjectionStatus::RepairPending);
    TEST_REQUIRE_EQ(node->dag()->status(), DagStatus::Ready);
    TEST_REQUIRE(!node->dag()->prepare_transaction(transaction, actor).has_value());

    auto shadow_batch = node->dag()->build_shadow_batch(SectionId(1), SectionId(20), {});
    TEST_REQUIRE(shadow_batch.has_value());
    ExtraChain::Consensus::Proposal shadow_proposal {
        .header =
            ExtraChain::Consensus::ConsensusHeader {
                .dag_section      = 20,
                .section_root     = control_20_before,
                .transaction_root = shadow_batch.value().manifest.transaction_root,
                .batch_root       = ExtraChain::Consensus::hash_batch_manifest(shadow_batch.value().manifest),
            },
        .batch = shadow_batch.value().manifest,
    };
    shadow_batch.value().header_hash  = ExtraChain::Consensus::hash_header(shadow_proposal.header);
    const auto calculated_shadow_root = node->dag()->shadow_batch_section_root(shadow_batch.value());
    TEST_REQUIRE(calculated_shadow_root.has_value());
    TEST_REQUIRE_EQ(calculated_shadow_root.value(), control_20_before);
    const auto shadow_validation =
        node->dag()->validate_shadow_batch(shadow_proposal, shadow_batch.value(), 16ULL * 1024ULL * 1024ULL);
    TEST_REQUIRE_MESSAGE(shadow_validation.has_value(),
                         shadow_validation.has_value()
                             ? std::string {}
                             : std::to_string(std::to_underlying(shadow_validation.error())));
    WireFormat::Scope speculative_scope(WireFormat::Mode::Canonical);
    const auto        speculative_parent =
        Json::serialize(Section { .id = SectionId(60), .transactions = {}, .control = std::nullopt });
    const auto speculative_empty_batch =
        node->dag()->build_shadow_intent_batch(SectionId(61),
                                               SectionId(80),
                                               2,
                                               std::vector<ExtraChain::Consensus::IntentEnvelope> {},
                                               {},
                                               speculative_parent,
                                               "speculative-parent-root");
    TEST_REQUIRE(speculative_empty_batch.has_value());
    TEST_REQUIRE(speculative_empty_batch.value().manifest.transaction_hashes.empty());
    TEST_REQUIRE_EQ(speculative_empty_batch.value().manifest.previous_section_root, "speculative-parent-root");
    TEST_REQUIRE(node->dag()->shadow_batch_section_root(speculative_empty_batch.value()).has_value());
    auto corrupted_shadow_batch = shadow_batch.value();
    corrupted_shadow_batch.sections.front().second.push_back('x');
    TEST_REQUIRE(!node->dag()
                      ->validate_shadow_batch(shadow_proposal, corrupted_shadow_batch, 16ULL * 1024ULL * 1024ULL)
                      .has_value());
    const auto historical_reward = make_reward(SectionId(5), 3);
    TEST_REQUIRE(node->dag()->save_transaction(historical_reward));
    const auto changed_control_20 = node->dag()->read_control(SectionId(20));
    const auto changed_control_40 = node->dag()->read_control(SectionId(40));
    TEST_REQUIRE(changed_control_20.has_value());
    TEST_REQUIRE(changed_control_40.has_value());
    const auto control_20_after = changed_control_20.value().control;
    const auto control_40_after = changed_control_40.value().control;
    TEST_REQUIRE(control_20_after != control_20_before);
    TEST_REQUIRE(control_40_after != control_40_before);

    TEST_REQUIRE(node->dag()->save_transaction(historical_reward));
    const auto unchanged_control_20 = node->dag()->read_control(SectionId(20));
    const auto unchanged_control_40 = node->dag()->read_control(SectionId(40));
    TEST_REQUIRE(unchanged_control_20.has_value());
    TEST_REQUIRE(unchanged_control_40.has_value());
    TEST_REQUIRE_EQ(unchanged_control_20.value().control, control_20_after);
    TEST_REQUIRE_EQ(unchanged_control_40.value().control, control_40_after);

    TEST_REQUIRE(node->dag()
                     ->install_shadow_batch(shadow_proposal, shadow_batch.value(), 16ULL * 1024ULL * 1024ULL)
                     .has_value());
    TEST_REQUIRE_EQ(node->dag()->state_projection().status, StateProjectionStatus::Ready);
    const auto installed_control_20   = node->dag()->read_control(SectionId(20));
    const auto invalidated_control_40 = node->dag()->read_control(SectionId(40));
    TEST_REQUIRE(installed_control_20.has_value());
    TEST_REQUIRE_EQ(installed_control_20.value().control, control_20_before);
    TEST_REQUIRE(!invalidated_control_40.has_value());

    node->dag()->cache().write_cached_balances({}, SectionId(123));
    TEST_REQUIRE_EQ(node->dag()->cache().section(), SectionId(123));
    node->dag()->cache().reset_db();
    TEST_REQUIRE_EQ(node->dag()->cache().section(), SectionId(-1));

    std::atomic<bool>        cache_stress_ok = true;
    std::vector<std::thread> cache_workers;
    for (std::uint64_t worker = 0; worker < 3; ++worker) {
        cache_workers.emplace_back([&, worker] {
            for (std::uint64_t iteration = 0; iteration < 50; ++iteration) {
                if (!node->dag()->cache().init_db()) {
                    cache_stress_ok.store(false);
                    return;
                }
                const BigNumberFloat balance(std::to_string(worker * 100 + iteration + 1));
                node->dag()->cache().write_cached_balance(actor.id(), actor.id(), balance);
                static_cast<void>(node->dag()->cache().read_cached_balance(actor.id(), actor.id()));
            }
        });
    }
    cache_workers.emplace_back([&] {
        for (std::uint64_t iteration = 0; iteration < 25; ++iteration) {
            node->dag()->cache().reset_db();
            if (!node->dag()->cache().init_db()) {
                cache_stress_ok.store(false);
                return;
            }
        }
    });
    for (auto &worker : cache_workers) {
        worker.join();
    }
    TEST_REQUIRE(cache_stress_ok.load());
    node->dag()->cache().write_cached_balance(actor.id(), actor.id(), BigNumberFloat("321"));
    TEST_REQUIRE_EQ(node->dag()->cache().read_cached_balance(actor.id(), actor.id()), BigNumberFloat("321"));

    node->dag()->set_status(DagStatus::Ready);
    const auto future_reward = make_reward(node->dag()->current_section() + SectionId(CACHE_LAG_SECTIONS + 1), 4);
    CapturingSender future_sender;
    Responder       future_responder(&future_sender);
    future_responder.add_identifier("future-transaction-peer");
    bool future_completed = false;
    bool future_forwarded = true;
    TEST_REQUIRE(node->dag()->submit_network_transaction(future_reward,
                                                         future_responder,
                                                         [&](std::expected<void, TransactionProveError> result,
                                                             bool should_forward) {
                                                             future_completed = result.has_value();
                                                             future_forwarded = should_forward;
                                                         }));
    node->dag()->flush_admission();
    TEST_REQUIRE(future_completed);
    TEST_REQUIRE(!future_forwarded);
    TEST_REQUIRE_EQ(future_sender.responses, std::size_t(0));
    TEST_REQUIRE_EQ(node->dag()->cached_txs_size(), std::size_t(1));

    CapturingSender duplicate_future_sender;
    Responder       duplicate_future_responder(&duplicate_future_sender);
    duplicate_future_responder.add_identifier("duplicate-future-transaction-peer");
    TEST_REQUIRE(node->dag()->submit_network_transaction(future_reward, duplicate_future_responder, {}));
    node->dag()->flush_admission();
    TEST_REQUIRE_EQ(node->dag()->cached_txs_size(), std::size_t(1));

    TEST_REQUIRE(node->dag()->save_transaction(future_reward));
    node->dag()->process_cached_transactions();
    TEST_REQUIRE_EQ(node->dag()->cached_txs_size(), std::size_t(0));
    TEST_REQUIRE_EQ(future_sender.responses, std::size_t(1));
    TEST_REQUIRE_EQ(duplicate_future_sender.responses, std::size_t(0));
    TEST_REQUIRE_EQ(future_sender.message_type, MessageType::DagTransactionResult);
    const auto future_result = MessagePack::deserialize<TransactionResult>(future_sender.payload);
    TEST_REQUIRE(future_result.has_value());
    TEST_REQUIRE_EQ(future_result->hash, future_reward.hash());
    TEST_REQUIRE_EQ(future_result->result, TransactionProveError::NoError);

    CapturingSender sender;
    Responder       responder(&sender);
    responder.add_identifier("missing-control-peer");
    node->dag()->network_request_control_section(DagControlRangeRequest { .from = SectionId(0),
                                                                          .to   = SectionId(20) },
                                                 responder);
    TEST_REQUIRE_EQ(sender.responses, std::size_t(1));
    TEST_REQUIRE_EQ(sender.message_type, MessageType::DagControlRangeResponse);

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original_path);
    std::filesystem::remove_all(test_path);
    return 0;
}
