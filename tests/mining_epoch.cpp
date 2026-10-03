#include "consensus/mining_epoch.h"
#include "test_support.h"
#include "utils/serialization.h"

#include <algorithm>
#include <limits>

using namespace ExtraChain::Consensus;

int main() {
    const auto schedule = mining_epoch_schedule(1).value();
    TEST_REQUIRE_EQ(schedule.first_section, std::uint64_t(21));
    TEST_REQUIRE_EQ(schedule.last_section, std::uint64_t(40));
    TEST_REQUIRE_EQ(schedule.challenge_section, std::uint64_t(60));
    TEST_REQUIRE_EQ(schedule.proof_first_section, std::uint64_t(101));
    TEST_REQUIRE_EQ(schedule.proof_last_section, std::uint64_t(140));
    TEST_REQUIRE_EQ(schedule.settlement_first_section, std::uint64_t(181));
    TEST_REQUIRE(!mining_epoch_schedule(UINT64_MAX).has_value());
    const auto network      = ActorId::create(std::string(40, '1')).value();
    const auto alice        = ActorId::create(std::string(40, '2')).value();
    const auto bob          = ActorId::create(std::string(40, '3')).value();
    const auto outsider     = ActorId::create(std::string(40, '4')).value();
    const auto small_reader = [](auto) -> std::expected<std::string, ConsensusError> {
        return "a";
    };
    const auto large_reader = [](auto) -> std::expected<std::string, ConsensusError> {
        return "abc";
    };
    const auto                      small    = commit_storage_dataset(1, small_reader).value();
    const auto                      large    = commit_storage_dataset(3, large_reader).value();
    const auto                      small_id = storage_dataset_id(network, small).value();
    const auto                      large_id = storage_dataset_id(network, large).value();
    std::vector<MiningRegistration> registrations { { alice, small, 20 },
                                                    { bob, small, 19 },
                                                    { alice, large, 20 },
                                                    { bob, large, 18 } };
    const auto                      frozen = freeze_mining_epoch(network, 1, 10, registrations);
    TEST_REQUIRE(frozen.has_value());
    TEST_REQUIRE_EQ(frozen.value().datasets.at(small_id).units, std::uint64_t(2));
    TEST_REQUIRE_EQ(frozen.value().datasets.at(large_id).units, std::uint64_t(7));
    const auto original_root = mining_epoch_root(frozen.value());
    std::ranges::reverse(registrations);
    registrations.push_back(registrations.front());
    TEST_REQUIRE_EQ(mining_epoch_root(freeze_mining_epoch(network, 1, 10, registrations).value()), original_root);
    registrations.front().section = 21;
    TEST_REQUIRE(!freeze_mining_epoch(network, 1, 10, registrations).has_value());
    TEST_REQUIRE(!freeze_mining_epoch(network, UINT64_MAX, 10, { }).has_value());
    TEST_REQUIRE(!freeze_mining_epoch(ActorId(), 1, 10, { }).has_value());
    TEST_REQUIRE(!freeze_mining_epoch(network, 1, MaximumMiningEmissionUnits + 1, { }).has_value());
    TEST_REQUIRE(
        !freeze_mining_epoch(network, 1, 10, std::vector<MiningRegistration>(MaximumMiningRegistrations + 1))
             .has_value());
    TEST_REQUIRE_EQ(reserve_mining_emission(0, MaximumMiningEmissionUnits).value(), MaximumMiningEmissionUnits);
    TEST_REQUIRE(!reserve_mining_emission(MaximumMiningEmissionUnits, 1).has_value());
    TEST_REQUIRE(!reserve_mining_emission(UINT64_MAX, 0).has_value());
    TEST_REQUIRE(!reserve_mining_emission(1, UINT64_MAX).has_value());
    const StorageDataset huge_a { .bytes = MaximumStorageDatasetBytes, .root = std::string(64, 'a') };
    const StorageDataset huge_b { .bytes = MaximumStorageDatasetBytes, .root = std::string(64, 'b') };
    const auto           huge = freeze_mining_epoch(network,
                                                    1,
                                                    MaximumMiningEmissionUnits,
                                                    { { alice, huge_a, 20 }, { alice, huge_b, 20 } });
    TEST_REQUIRE(huge.has_value());
    for (const auto& [id, dataset] : huge.value().datasets)
        TEST_REQUIRE_EQ(dataset.units, MaximumMiningEmissionUnits / 2);

    auto             state = frozen.value();
    StorageChallenge challenge { .epoch = 1, .checkpoint = std::string(64, 'a') };
    const auto       alice_small = make_storage_proof(network, alice, small, challenge, small_reader).value();
    const auto       bob_small   = make_storage_proof(network, bob, small, challenge, small_reader).value();
    const auto       alice_large = make_storage_proof(network, alice, large, challenge, large_reader).value();
    TEST_REQUIRE(!accept_mining_proof(state, alice, small_id, 100, alice_small).has_value());
    TEST_REQUIRE(!settle_mining_epoch(state, 1000).has_value());
    TEST_REQUIRE(state.rewards.empty());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 40).has_value());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 81).has_value());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 121).has_value());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, UINT64_MAX).has_value());
    auto wrong_challenge = challenge;
    wrong_challenge.epoch += 1;
    TEST_REQUIRE(!open_mining_proof_window(state, wrong_challenge, 101).has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(state), original_root);
    TEST_REQUIRE(open_mining_proof_window(state, challenge, 101).has_value());
    TEST_REQUIRE_EQ(state.proof_last_section, std::uint64_t(140));
    const auto open_root = mining_epoch_root(state);
    TEST_REQUIRE(open_root != original_root);
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 121).has_value());
    TEST_REQUIRE(!accept_mining_proof(state, alice, small_id, 100, alice_small).has_value());
    TEST_REQUIRE(!accept_mining_proof(state, alice, small_id, 141, alice_small).has_value());
    TEST_REQUIRE(!accept_mining_proof(state, outsider, small_id, 120, alice_small).has_value());
    TEST_REQUIRE(!accept_mining_proof(state, alice, "missing", 120, alice_small).has_value());
    auto damaged                  = alice_small;
    damaged.samples.front().bytes = "b";
    TEST_REQUIRE(!accept_mining_proof(state, alice, small_id, 101, damaged).has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(state), open_root);
    TEST_REQUIRE(accept_mining_proof(state, alice, small_id, 101, alice_small).has_value());
    TEST_REQUIRE(!accept_mining_proof(state, alice, small_id, 120, alice_small).has_value());
    TEST_REQUIRE(accept_mining_proof(state, bob, small_id, 140, bob_small).has_value());
    TEST_REQUIRE(accept_mining_proof(state, alice, large_id, 140, alice_large).has_value());
    auto restored = MessagePack::deserialize<MiningEpochState>(MessagePack::serialize(state));
    TEST_REQUIRE(restored.has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(restored.value()), mining_epoch_root(state));
    TEST_REQUIRE(!settle_mining_epoch(state, 139).has_value());
    TEST_REQUIRE(settle_mining_epoch(state, 140).has_value());
    TEST_REQUIRE(settle_mining_epoch(restored.value(), 140).has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(restored.value()), mining_epoch_root(state));
    TEST_REQUIRE_EQ(state.rewards.at(alice.to_string()), std::uint64_t(8));
    TEST_REQUIRE_EQ(state.rewards.at(bob.to_string()), std::uint64_t(1));
    const auto settled_root = mining_epoch_root(state);
    const auto repeated     = settle_mining_epoch(state, 160);
    TEST_REQUIRE(!repeated.has_value() && repeated.error() == ConsensusError::Replay);
    TEST_REQUIRE_EQ(mining_epoch_root(state), settled_root);
    TEST_REQUIRE(!accept_mining_proof(state, bob, large_id, 140, alice_large).has_value());
    TEST_REQUIRE(!state.rewards.contains(outsider.to_string()));
    const auto settled_copy = MessagePack::deserialize<MiningEpochState>(MessagePack::serialize(state));
    TEST_REQUIRE(settled_copy.has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(settled_copy.value()), settled_root);

    auto unused = frozen.value();
    TEST_REQUIRE(open_mining_proof_window(unused, challenge, 101).has_value());
    TEST_REQUIRE(accept_mining_proof(unused, alice, small_id, 101, alice_small).has_value());
    TEST_REQUIRE(settle_mining_epoch(unused, 140).has_value());
    TEST_REQUIRE_EQ(unused.rewards.size(), std::size_t(1));
    TEST_REQUIRE_EQ(unused.rewards.at(alice.to_string()), std::uint64_t(2));
    auto empty = freeze_mining_epoch(network, 1, 10, { }).value();
    TEST_REQUIRE(open_mining_proof_window(empty, challenge, 101).has_value());
    TEST_REQUIRE(settle_mining_epoch(empty, 140).has_value());
    TEST_REQUIRE(empty.rewards.empty());
}
