#pragma once

#include "consensus/consensus_types.h"

namespace ExtraChain::Consensus {
    inline constexpr std::uint64_t NativeCoinUnits               = 100'000'000;
    inline constexpr std::uint64_t MaximumMiningEmissionUnits    = 10'000 * NativeCoinUnits;
    inline constexpr std::size_t   MaximumMiningEmissionSegments = 256;
    inline constexpr std::uint64_t DefaultMiningEpochMs          = 30'000;
    inline constexpr std::uint64_t DefaultMiningProofWindowMs    = 10'000;
    inline constexpr std::uint64_t MaximumMiningPeriodMs         = 86'400'000;

    struct MiningEmissionSegment {
        std::uint64_t epochs          = 0;
        std::uint64_t units_per_epoch = 0;

        MSGPACK_DEFINE(epochs, units_per_epoch)
    };

    struct MiningEmissionPolicy {
        std::uint64_t                      first_epoch = 0;
        std::vector<MiningEmissionSegment> segments;
        // Epoch e covers parent block times [e * epoch_ms, (e + 1) * epoch_ms), so emission follows time,
        // not the block rate; a proof window stays open proof_window_ms of block time.
        std::uint64_t epoch_ms        = DefaultMiningEpochMs;
        std::uint64_t proof_window_ms = DefaultMiningProofWindowMs;

        MSGPACK_DEFINE(first_epoch, segments, epoch_ms, proof_window_ms)
    };

    std::expected<std::uint64_t, ConsensusError> mining_policy_total(const MiningEmissionPolicy& policy);
    std::string mining_policy_hash(const std::optional<MiningEmissionPolicy>& policy);
    std::expected<std::uint64_t, ConsensusError> mining_policy_budget(const MiningEmissionPolicy& policy,
                                                                      std::uint64_t               epoch);
} // namespace ExtraChain::Consensus
