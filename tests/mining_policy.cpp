#include "consensus/mining_policy.h"
#include "test_support.h"
#include "utils/serialization.h"

using namespace ExtraChain::Consensus;

int main() {
    const MiningEmissionPolicy policy { 10, { { 2, 7 }, { 3, 0 }, { 4, 3 } } };
    TEST_REQUIRE_EQ(mining_policy_total(policy).value(), std::uint64_t(26));
    for (const auto epoch : { 0, 9, 12, 13, 14, 19, 20 })
        TEST_REQUIRE_EQ(mining_policy_budget(policy, epoch).value(), std::uint64_t(0));
    for (const auto epoch : { 10, 11 })
        TEST_REQUIRE_EQ(mining_policy_budget(policy, epoch).value(), std::uint64_t(7));
    for (const auto epoch : { 15, 16, 17, 18 })
        TEST_REQUIRE_EQ(mining_policy_budget(policy, epoch).value(), std::uint64_t(3));
    TEST_REQUIRE_EQ(mining_policy_budget(policy, UINT64_MAX).value(), std::uint64_t(0));
    const auto restored = MessagePack::deserialize<MiningEmissionPolicy>(MessagePack::serialize(policy));
    TEST_REQUIRE(restored.has_value());
    TEST_REQUIRE_EQ(mining_policy_total(restored.value()).value(), std::uint64_t(26));
    TEST_REQUIRE_EQ(mining_policy_total({ 0, { { 10'000, NativeCoinUnits } } }).value(),
                    MaximumMiningEmissionUnits);
    for (const MiningEmissionPolicy invalid :
         { MiningEmissionPolicy { },
           MiningEmissionPolicy { 0, { { 0, 1 } } },
           MiningEmissionPolicy { UINT64_MAX, { { 1, 1 } } },
           MiningEmissionPolicy { 1, { { UINT64_MAX, 0 } } },
           MiningEmissionPolicy { 0, { { UINT64_MAX, UINT64_MAX } } },
           MiningEmissionPolicy { 0, { { 10'001, NativeCoinUnits } } },
           MiningEmissionPolicy { 0, { { 1, MaximumMiningEmissionUnits }, { 1, 1 } } },
           MiningEmissionPolicy {
               0,
               std::vector<MiningEmissionSegment>(MaximumMiningEmissionSegments + 1, { 1, 1 }) } }) {
        TEST_REQUIRE(!mining_policy_total(invalid).has_value());
        TEST_REQUIRE(!mining_policy_budget(invalid, 0).has_value());
    }
    TEST_REQUIRE(
        mining_policy_total({ 0, std::vector<MiningEmissionSegment>(MaximumMiningEmissionSegments, { 1, 1 }) })
            .has_value());
}
