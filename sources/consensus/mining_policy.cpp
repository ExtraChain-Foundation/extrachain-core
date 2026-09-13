#include "consensus/mining_policy.h"
#include "consensus/mining_epoch.h"

namespace ExtraChain::Consensus {
    std::expected<std::uint64_t, ConsensusError> mining_policy_total(const MiningEmissionPolicy& policy) {
        if (policy.segments.empty() || policy.segments.size() > MaximumMiningEmissionSegments)
            return std::unexpected(ConsensusError::InvalidIntent);
        std::uint64_t next_epoch = policy.first_epoch;
        std::uint64_t total      = 0;
        for (const auto& segment : policy.segments) {
            if (segment.epochs == 0 || segment.epochs > UINT64_MAX - next_epoch
                || !mining_epoch_schedule(next_epoch + segment.epochs - 1).has_value()
                || segment.units_per_epoch > (MaximumMiningEmissionUnits - total) / segment.epochs)
                return std::unexpected(ConsensusError::InvalidIntent);
            total += segment.epochs * segment.units_per_epoch;
            next_epoch += segment.epochs;
        }
        return total;
    }

    std::expected<std::uint64_t, ConsensusError> mining_policy_budget(const MiningEmissionPolicy& policy,
                                                                      std::uint64_t               epoch) {
        const auto total = mining_policy_total(policy);
        if (!total.has_value())
            return std::unexpected(total.error());
        if (epoch < policy.first_epoch)
            return 0;
        auto offset = epoch - policy.first_epoch;
        for (const auto& segment : policy.segments) {
            if (offset < segment.epochs)
                return segment.units_per_epoch;
            offset -= segment.epochs;
        }
        return 0;
    }
} // namespace ExtraChain::Consensus
