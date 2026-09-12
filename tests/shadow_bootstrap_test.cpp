#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <string_view>
#include <thread>

#include "chain/actor.h"
#include "chain/dag.h"
#include "consensus/consensus_service.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "network/responder.h"
#include "test_support.h"

namespace {

    int callback_bootstrap(const char* home, const char* login_home) {
        std::filesystem::current_path(home);
        auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
        node->process();
        node->dag()->set_mode(DagMode::Full);
        node->dag()->start();
        // Bootstrap can schedule pack work; its storage workers must remain alive until cleanup.
        std::promise<void> storage_ready;
        auto               storage_result = storage_ready.get_future();
        node->post_storage([&] {
            storage_ready.set_value();
        });
        TEST_REQUIRE(storage_result.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        std::promise<void> entered;
        std::atomic<bool>  completed     = false;
        std::atomic<bool>  callback_done = false;
        std::thread        watchdog([&] {
            for (int i = 0; i < 2400 && !completed.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!completed.load()) {
                std::fprintf(stderr, "FAIL: bootstrap did not complete within 120 seconds\n");
                std::abort();
            }
        });
        Transaction        invalid;
        Responder          responder(node->network());
        const bool         queued = node->dag()->submit_network_transaction(invalid, responder, [&](auto, bool) {
            std::fprintf(stderr, "CALLBACK ENTERED\n");
            entered.set_value();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            // Bootstrap must let an admission callback acquire the consensus mutex.
            const bool active = node->consensus()->active();
            std::fprintf(stderr, "CALLBACK COMPLETE active=%d\n", active);
            callback_done.store(true);
        });
        TEST_REQUIRE(queued);
        entered.get_future().wait();
        std::fprintf(stderr, "LOGIN AND BOOTSTRAP\n");
        const auto login = node->login(Utils::calculate_hash(std::string(login_home) + ":joiner"));
        node->dag()->flush_admission();
        const bool active = node->consensus()->active();
        const bool voting = node->consensus()->voting();
        const bool pass   = login.has_value() && callback_done.load() && active && !voting;
        std::fprintf(stderr,
                     "RESULT login=%d callback=%d active=%d voting=%d\n",
                     login.has_value(),
                     callback_done.load(),
                     active,
                     voting);
        node->cleanUp();
        completed.store(true);
        watchdog.join();
        return pass ? 0 : 1;
    }

} // namespace

int main(int argc, char** argv) {
    if ((argc == 3 || argc == 4) && std::string_view(argv[1]) == "--callback-bootstrap") {
        return callback_bootstrap(argv[2], argc == 4 ? argv[3] : argv[2]);
    }
    if (argc != 1) {
        return 64;
    }
    using ExtraChain::Consensus::ConsensusError;
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-shadow-bootstrap-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    auto* dag = node->dag();
    dag->set_mode(DagMode::Full);
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    node->account_controller()->create_profile("shadow-bootstrap-profile", ActorType::User, actor);
    for (int section = 0; section <= 45; ++section) {
        Transaction reward;
        reward.set_sender(actor.id());
        reward.set_receiver(actor.id());
        reward.set_token(actor.id());
        reward.set_type(TransactionType::Reward);
        reward.set_amount(BigNumberFloat("1"));
        reward.set_section(SectionId(section));
        reward.set_timestamp(static_cast<std::uint64_t>(section + 1));
        TEST_REQUIRE(reward.sign(actor));
        TEST_REQUIRE(dag->save_transaction(reward));
    }
    dag->flush_admission();
    dag->set_status(DagStatus::Ready);
    for (const int boundary : { 0, 21, 60 }) {
        const auto rejected = dag->prepare_shadow_activation(SectionId(boundary));
        TEST_REQUIRE(!rejected.has_value());
        TEST_REQUIRE_EQ(rejected.error(), ConsensusError::InvalidHeight);
    }
    // A later cache must be replayed from the beginning, not relabeled as the prefix.
    const auto later = dag->cache().update_to_genesis_section(SectionId(40),
                                                              SectionId(45),
                                                              SectionId(0),
                                                              [dag](const SectionId& section) {
                                                                  return dag->read_section(section);
                                                              });
    TEST_REQUIRE(later.first);
    TEST_REQUIRE_EQ(dag->cache().section(), SectionId(40));
    const auto prepared = dag->prepare_shadow_activation(SectionId(20));
    TEST_REQUIRE(prepared.has_value());
    TEST_REQUIRE_EQ(prepared.value(), SectionId(20));
    TEST_REQUIRE_EQ(dag->current_section(), SectionId(45));
    TEST_REQUIRE_EQ(dag->cache().section(), SectionId(20));
    TEST_REQUIRE_EQ(dag->state_projection().verified_section, SectionId(20));
    TEST_REQUIRE(dag->read_control(SectionId(20)).has_value());
    const auto snapshot = dag->cache().read_cached_balances();
    TEST_REQUIRE_EQ(snapshot.second.at({ actor.id(), actor.id() }), BigNumberFloat("21"));
    TEST_REQUIRE(dag->read_section(SectionId(45)).has_value());
    Transaction legacy;
    legacy.set_type(TransactionType::Reward);
    legacy.set_sender(actor.id());
    legacy.set_receiver(actor.id());
    legacy.set_token(actor.id());
    legacy.set_section(SectionId(46));
    legacy.set_amount(BigNumberFloat(1));
    TEST_REQUIRE(legacy.sign(actor));
    const auto legacy_result = dag->network_transaction(legacy, Responder(nullptr));
    TEST_REQUIRE(!legacy_result.has_value());
    TEST_REQUIRE_EQ(legacy_result.error(), TransactionProveError::IntentRequired);
    TEST_REQUIRE(!dag->read_section(SectionId(46)).has_value());
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
