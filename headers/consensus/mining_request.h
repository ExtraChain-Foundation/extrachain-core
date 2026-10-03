#pragma once

#include <variant>

#include "consensus/mining_state.h"

namespace ExtraChain::Consensus {
    struct MiningProofSubmission {
        std::uint64_t epoch = 0;
        std::string   dataset_id;
        StorageProof  proof;

        MSGPACK_DEFINE(epoch, dataset_id, proof)
    };

    using MiningRequest = std::variant<StorageDataset, std::string, MiningProofSubmission>;

    bool                                         is_mining_operation(IntentOperation operation);
    std::expected<MiningRequest, ConsensusError> decode_mining_request(const IntentEnvelope& envelope);
    // Signature and account nonce admission must succeed before this state transition.
    std::expected<void, ConsensusError> apply_mining_request(MiningState& state, const IntentEnvelope& envelope);
} // namespace ExtraChain::Consensus
