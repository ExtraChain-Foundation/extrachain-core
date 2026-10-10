#pragma once

#include "consensus/storage_proof.h"

namespace ExtraChain::Consensus {
    std::expected<StorageDataset, ConsensusError> write_storage_index(const std::filesystem::path& path,
                                                                      std::uint64_t                bytes,
                                                                      const MerkleValueReader&     read_chunk);
    std::expected<StorageProof, ConsensusError> make_storage_proof_from_index(const std::filesystem::path& path,
                                                                              const ActorId&               network,
                                                                              const ActorId&           provider,
                                                                              const StorageDataset&    dataset,
                                                                              const StorageChallenge&  challenge,
                                                                              const MerkleValueReader& read_chunk);
    // One proof for all of a provider's datasets, each with its index file at the same position.
    std::expected<StorageProof, ConsensusError> make_provider_proof_from_indexes(
        std::span<const std::filesystem::path> paths,
        const ActorId&                         network,
        const ActorId&                         provider,
        std::span<const StorageDataset>        datasets,
        const StorageChallenge&                challenge,
        const StorageChunkSource&              read_chunk);
} // namespace ExtraChain::Consensus
