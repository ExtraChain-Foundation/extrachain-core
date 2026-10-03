#pragma once

#include "consensus/mining_state.h"

namespace ExtraChain::Consensus {
    struct MiningSettlementRecord {
        MiningEpochWitness witness;
        FinalityProof      closure;

        MSGPACK_DEFINE(witness, closure)
    };

    // The ledger must enforce one record per epoch at its fixed settlement section.
    std::expected<MiningPayouts, ConsensusError> verify_mining_settlement(const MiningSettlementRecord& record,
                                                                          std::uint64_t                 section,
                                                                          const LightClientVerifier&    verifier);
} // namespace ExtraChain::Consensus
