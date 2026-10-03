#include "consensus/mining_settlement.h"
#include "consensus/light_client.h"

namespace ExtraChain::Consensus {
    std::expected<MiningPayouts, ConsensusError> verify_mining_settlement(const MiningSettlementRecord& record,
                                                                          std::uint64_t                 section,
                                                                          const LightClientVerifier&    verifier) {
        const auto  schedule = mining_epoch_schedule(record.witness.epoch.epoch);
        const auto& proposal = record.closure.finalized_proposal;
        if (!schedule.has_value() || section != schedule.value().settlement_first_section
            || record.witness.epoch.settled || !record.witness.epoch.rewards.empty()
            || !record.witness.epoch.claimed.empty()
            || !verify_mining_epoch_witness(record.witness, proposal.state.mining_state_root)
            || hash_state_commitment(proposal.state) != proposal.header.state_commitment)
            return std::unexpected(ConsensusError::InvalidProof);
        auto       epoch   = record.witness.epoch;
        const auto settled = settle_mining_epoch(epoch, record.closure, verifier);
        if (!settled.has_value())
            return std::unexpected(settled.error());
        return std::move(epoch.rewards);
    }
} // namespace ExtraChain::Consensus
