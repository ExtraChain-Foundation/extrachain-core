#include "consensus/mining_state.h"
#include "test_support.h"
#include "utils/exc_utils.h"
#include "utils/serialization.h"

#include <vector>

using namespace ExtraChain::Consensus;

int main() {
    const auto           network  = ActorId::create(std::string(40, 'a')).value();
    const auto           provider = ActorId::create(std::string(40, 'b')).value();
    const StorageDataset dataset { .bytes = 123, .root = std::string(64, 'c') };
    auto                 state = create_mining_state(network, 0).value();
    TEST_REQUIRE(register_storage_provider(state, provider, dataset).has_value());
    const auto identity = storage_dataset_id(network, dataset).value();
    state.epochs.emplace(0, freeze_mining_epoch(network, 0, 123, { { provider, dataset, 0 } }).value());
    state.epochs.emplace(1, freeze_mining_epoch(network, 1, 124, { { provider, dataset, 0 } }).value());
    const auto root = mining_state_root(state);
    TEST_REQUIRE(!root.empty());
    for (const auto epoch : { 0, 1 }) {
        const auto witness = make_mining_epoch_witness(state, epoch);
        TEST_REQUIRE(witness.has_value());
        TEST_REQUIRE(verify_mining_epoch_witness(witness.value(), root));
        auto restored = MessagePack::deserialize<MiningEpochWitness>(MessagePack::serialize(witness.value()));
        TEST_REQUIRE(restored.has_value());
        TEST_REQUIRE(verify_mining_epoch_witness(restored.value(), root));
        auto changed = witness.value();
        changed.epoch.budget_units += 1;
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, root));
        changed = witness.value();
        changed.epoch.epoch += 1;
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, root));
        changed = witness.value();
        changed.epoch.datasets.at(identity).accepted.insert(provider.to_string());
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, root));
        changed               = witness.value();
        changed.epoch.network = provider;
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, root));
        changed = witness.value();
        changed.membership.siblings.front()[0] ^= 1;
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, root));
        changed                       = witness.value();
        changed.membership.leaf_count = UINT64_MAX;
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, root));
        auto other_state = state;
        other_state.reserved_units += 1;
        TEST_REQUIRE(!verify_mining_epoch_witness(witness.value(), mining_state_root(other_state)));
    }
    TEST_REQUIRE(!make_mining_epoch_witness(state, 2).has_value());

    std::vector<MiningRegistration> registrations;
    auto                            maximum = create_mining_state(network, 0).value();
    for (std::size_t index = 0; index < MaximumMiningRegistrations; ++index) {
        const auto actor = ActorId::create(Utils::calculate_hash(std::to_string(index)).substr(0, 40)).value();
        const StorageDataset data { .bytes = 1 + index / 8, .root = std::string(64, 'd') };
        TEST_REQUIRE(register_storage_provider(maximum, actor, data).has_value());
        registrations.push_back({ actor, data, 0 });
    }
    TEST_REQUIRE_EQ(maximum.registrations.size(), MaximumMiningDatasets);
    const auto before_overflow = mining_state_root(maximum);
    TEST_REQUIRE(!register_storage_provider(maximum, provider, dataset).has_value());
    TEST_REQUIRE_EQ(mining_state_root(maximum), before_overflow);
    auto epoch = freeze_mining_epoch(network, 0, MaximumMiningEmissionUnits, registrations).value();
    for (auto& [id, budget] : epoch.datasets)
        budget.accepted = budget.providers;
    maximum.epochs.emplace(0, std::move(epoch));
    const auto maximum_root = mining_state_root(maximum);
    const auto proof        = make_mining_epoch_witness(maximum, 0).value();
    TEST_REQUIRE(verify_mining_epoch_witness(proof, maximum_root));
    const auto encoded = MessagePack::serialize(proof);
    TEST_REQUIRE(encoded.size() < 1024 * 1024);
    const auto decoded = MessagePack::deserialize<MiningEpochWitness>(encoded);
    TEST_REQUIRE(decoded.has_value());
    TEST_REQUIRE(verify_mining_epoch_witness(decoded.value(), maximum_root));
    const auto snapshot = MessagePack::deserialize<MiningState>(MessagePack::serialize(maximum));
    TEST_REQUIRE(snapshot.has_value());
    TEST_REQUIRE_EQ(mining_state_root(snapshot.value()), maximum_root);
    std::printf("maximum mining witness: %zu bytes, %zu datasets, %zu registrations\n",
                encoded.size(),
                maximum.registrations.size(),
                registrations.size());
}
