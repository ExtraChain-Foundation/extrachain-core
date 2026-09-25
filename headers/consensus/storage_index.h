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
} // namespace ExtraChain::Consensus
