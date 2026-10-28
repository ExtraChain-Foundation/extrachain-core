#include "consensus/mining_epoch.h"
#include "test_support.h"
#include "utils/serialization.h"

#include <algorithm>
#include <limits>

using namespace ExtraChain::Consensus;

int main() {
    // An epoch records its schedule as block time passes: the window opens once the challenge height
    // has two certified descendants, and settles once the window's last height has them.
    MiningEpochState timeline;
    TEST_REQUIRE(!mining_window_opening_section(timeline).has_value());
    TEST_REQUIRE(!mining_settlement_section(timeline).has_value());
    timeline.challenge_section = 60;
    TEST_REQUIRE_EQ(mining_window_opening_section(timeline).value(), std::uint64_t(101));
    timeline.proof_last_section = 140;
    TEST_REQUIRE_EQ(mining_settlement_section(timeline).value(), std::uint64_t(181));
    timeline.challenge_section = UINT64_MAX;
    TEST_REQUIRE(!mining_window_opening_section(timeline).has_value());
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
    const auto                      frozen = freeze_mining_epoch(network, 1, 10, registrations, 21);
    TEST_REQUIRE(frozen.has_value());
    TEST_REQUIRE_EQ(frozen.value().datasets.at(small_id).units, std::uint64_t(2));
    TEST_REQUIRE_EQ(frozen.value().datasets.at(large_id).units, std::uint64_t(7));
    const auto original_root = mining_epoch_root(frozen.value());
    std::ranges::reverse(registrations);
    registrations.push_back(registrations.front());
    TEST_REQUIRE_EQ(mining_epoch_root(freeze_mining_epoch(network, 1, 10, registrations, 21).value()),
                    original_root);
    registrations.front().section = 21;
    TEST_REQUIRE(!freeze_mining_epoch(network, 1, 10, registrations, 21).has_value());
    TEST_REQUIRE(!freeze_mining_epoch(network, UINT64_MAX, 10, { }, 21).has_value());
    TEST_REQUIRE(!freeze_mining_epoch(network, 1, 10, { }, 0).has_value());
    TEST_REQUIRE(!freeze_mining_epoch(ActorId(), 1, 10, { }, 21).has_value());
    TEST_REQUIRE(!freeze_mining_epoch(network, 1, MaximumMiningEmissionUnits + 1, { }, 21).has_value());
    TEST_REQUIRE(
        !freeze_mining_epoch(network, 1, 10, std::vector<MiningRegistration>(MaximumMiningRegistrations + 1), 21)
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
                                                    { { alice, huge_a, 20 }, { alice, huge_b, 20 } },
                                                    21);
    TEST_REQUIRE(huge.has_value());
    for (const auto& [id, dataset] : huge.value().datasets)
        TEST_REQUIRE_EQ(dataset.units, MaximumMiningEmissionUnits / 2);

    auto             state = frozen.value();
    StorageChallenge challenge { .epoch = 1, .checkpoint = std::string(64, 'a') };
    // One proof per provider covers every dataset it holds in the epoch: both, for alice and bob.
    const auto prove = [&](const ActorId& provider) {
        const auto datasets = mining_provider_datasets(state, provider);
        return make_provider_storage_proof(network,
                                           provider,
                                           datasets,
                                           challenge,
                                           [&](std::size_t index, std::uint64_t chunk) {
                                               return datasets[index].root == small.root ? small_reader(chunk)
                                                                                         : large_reader(chunk);
                                           })
            .value();
    };
    TEST_REQUIRE_EQ(mining_provider_datasets(state, alice).size(), std::size_t(2));
    TEST_REQUIRE(mining_provider_datasets(state, outsider).empty());
    const auto alice_proof = prove(alice);
    const auto bob_proof   = prove(bob);
    TEST_REQUIRE(!accept_mining_proof(state, alice, 100, alice_proof).has_value());
    TEST_REQUIRE(!settle_mining_epoch(state, 1000).has_value());
    TEST_REQUIRE(state.rewards.empty());
    // No window before the next epoch has named its challenge height.
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 101, 1000).has_value());
    state.challenge_section = 60;
    const auto challenged_root = mining_epoch_root(state);
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 40, 1000).has_value());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 81, 1000).has_value());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 121, 1000).has_value());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, UINT64_MAX, 1000).has_value());
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 101, 0).has_value());
    auto wrong_challenge = challenge;
    wrong_challenge.epoch += 1;
    TEST_REQUIRE(!open_mining_proof_window(state, wrong_challenge, 101, 1000).has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(state), challenged_root);
    TEST_REQUIRE(open_mining_proof_window(state, challenge, 101, 1000).has_value());
    TEST_REQUIRE_EQ(state.proof_closes_ms, std::uint64_t(1000));
    TEST_REQUIRE_EQ(state.proof_last_section, std::uint64_t(0));
    const auto open_root = mining_epoch_root(state);
    TEST_REQUIRE(open_root != challenged_root);
    TEST_REQUIRE(!open_mining_proof_window(state, challenge, 101, 1000).has_value());
    TEST_REQUIRE(!accept_mining_proof(state, alice, 100, alice_proof).has_value());
    TEST_REQUIRE(!accept_mining_proof(state, outsider, 120, alice_proof).has_value());
    // These datasets have two chunks in all, so every chunk is sampled and alice's proof would pass for bob
    // too: a proof binds to its provider only once there are more chunks than samples (storage-proof).
    auto damaged                  = alice_proof;
    damaged.samples.front().bytes = "b";
    TEST_REQUIRE(!accept_mining_proof(state, alice, 101, damaged).has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(state), open_root);
    TEST_REQUIRE(accept_mining_proof(state, alice, 101, alice_proof).has_value());
    const auto replay = accept_mining_proof(state, alice, 120, alice_proof);
    TEST_REQUIRE(!replay.has_value() && replay.error() == ConsensusError::Replay);
    // An open window has no last section yet and cannot settle; block time then closes it.
    TEST_REQUIRE(!settle_mining_epoch(state, 1000).has_value());
    state.proof_last_section = 140;
    TEST_REQUIRE(!accept_mining_proof(state, bob, 141, bob_proof).has_value());
    TEST_REQUIRE(accept_mining_proof(state, bob, 140, bob_proof).has_value());
    auto restored = MessagePack::deserialize<MiningEpochState>(MessagePack::serialize(state));
    TEST_REQUIRE(restored.has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(restored.value()), mining_epoch_root(state));
    TEST_REQUIRE(!settle_mining_epoch(state, 139).has_value());
    TEST_REQUIRE(settle_mining_epoch(state, 140).has_value());
    TEST_REQUIRE(settle_mining_epoch(restored.value(), 140).has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(restored.value()), mining_epoch_root(state));
    // Both proved both datasets: 2 units of the small one and 7 of the large one split in halves.
    TEST_REQUIRE_EQ(state.rewards.at(alice.to_string()), std::uint64_t(4));
    TEST_REQUIRE_EQ(state.rewards.at(bob.to_string()), std::uint64_t(4));
    const auto settled_root = mining_epoch_root(state);
    const auto repeated     = settle_mining_epoch(state, 160);
    TEST_REQUIRE(!repeated.has_value() && repeated.error() == ConsensusError::Replay);
    TEST_REQUIRE_EQ(mining_epoch_root(state), settled_root);
    TEST_REQUIRE(!accept_mining_proof(state, bob, 140, bob_proof).has_value());
    TEST_REQUIRE(!state.rewards.contains(outsider.to_string()));
    const auto settled_copy = MessagePack::deserialize<MiningEpochState>(MessagePack::serialize(state));
    TEST_REQUIRE(settled_copy.has_value());
    TEST_REQUIRE_EQ(mining_epoch_root(settled_copy.value()), settled_root);

    auto unused              = frozen.value();
    unused.challenge_section = 60;
    TEST_REQUIRE(open_mining_proof_window(unused, challenge, 101, 1000).has_value());
    TEST_REQUIRE(accept_mining_proof(unused, alice, 101, alice_proof).has_value());
    unused.proof_last_section = 140;
    TEST_REQUIRE(settle_mining_epoch(unused, 140).has_value());
    TEST_REQUIRE_EQ(unused.rewards.size(), std::size_t(1));
    TEST_REQUIRE_EQ(unused.rewards.at(alice.to_string()), std::uint64_t(9));
    auto empty              = freeze_mining_epoch(network, 1, 10, { }, 21).value();
    empty.challenge_section = 60;
    TEST_REQUIRE(open_mining_proof_window(empty, challenge, 101, 1000).has_value());
    empty.proof_last_section = 140;
    TEST_REQUIRE(settle_mining_epoch(empty, 140).has_value());
    TEST_REQUIRE(empty.rewards.empty());
}
