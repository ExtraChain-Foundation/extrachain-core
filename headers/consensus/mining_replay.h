#pragma once

#include "consensus/mining_request.h"
#include "consensus/mining_transaction.h"

namespace ExtraChain::Consensus {
    std::expected<MiningState, ConsensusError> configure_mining_state(
        const ActorId&                             network,
        std::uint64_t                              boundary,
        const std::optional<MiningEmissionPolicy>& policy);

    // The caller verifies transaction signatures and account nonces before accepting the batch.
    std::expected<MiningState, ConsensusError> replay_mining_batch(
        MiningState                                state,
        const std::optional<MiningEmissionPolicy>& policy,
        const SectionBatchData&                    batch,
        const MiningFinalityReader&                read_finality,
        const LightClientVerifier&                 verifier);
} // namespace ExtraChain::Consensus
