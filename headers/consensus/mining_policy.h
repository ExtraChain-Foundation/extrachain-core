#pragma once

#include "consensus/consensus_types.h"

namespace ExtraChain::Consensus {
    inline constexpr std::uint64_t NativeCoinUnits               = 100'000'000;
    inline constexpr std::uint64_t MaximumMiningEmissionUnits    = 10'000 * NativeCoinUnits;
    inline constexpr std::size_t   MaximumMiningEmissionSegments = 256;

    struct MiningEmissionSegment {
        std::uint64_t epochs          = 0;
        std::uint64_t units_per_epoch = 0;

        MSGPACK_DEFINE(epochs, units_per_epoch)
    };

    struct MiningEmissionPolicy {
        std::uint64_t                      first_epoch = 0;
        std::vector<MiningEmissionSegment> segments;

        MSGPACK_DEFINE(first_epoch, segments)
    };

    std::expected<std::uint64_t, ConsensusError> mining_policy_total(const MiningEmissionPolicy& policy);
    std::string mining_policy_hash(const std::optional<MiningEmissionPolicy>& policy);
    std::expected<std::uint64_t, ConsensusError> mining_policy_budget(const MiningEmissionPolicy& policy,
                                                                      std::uint64_t               epoch);
} // namespace ExtraChain::Consensus
