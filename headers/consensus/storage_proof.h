#pragma once

#include "consensus/consensus_protocol.h"

namespace ExtraChain::Consensus {

    inline constexpr std::uint32_t StorageChunkBytes          = 16 * 1024;
    inline constexpr std::size_t   StorageChallengeSamples    = 8;
    inline constexpr std::uint64_t MaximumStorageDatasetBytes = (std::uint64_t(1) << 32) * StorageChunkBytes;

    struct StorageDataset {
        std::uint16_t version = 1;
        std::uint64_t bytes   = 0;
        std::string   root;

        MSGPACK_DEFINE(version, bytes, root)
    };

    struct StorageChallenge {
        std::uint64_t epoch = 0;
        std::string   checkpoint;

        MSGPACK_DEFINE(epoch, checkpoint)
    };

    struct StorageSample {
        std::string bytes;
        MerkleProof path;

        MSGPACK_DEFINE(bytes, path)
    };

    struct StorageProof {
        std::vector<StorageSample> samples;

        MSGPACK_DEFINE(samples)
    };

    std::expected<StorageDataset, ConsensusError> commit_storage_dataset(std::uint64_t            bytes,
                                                                         const MerkleValueReader& read_chunk);
    std::expected<std::string, ConsensusError>    storage_dataset_id(const ActorId&        network,
                                                                     const StorageDataset& dataset);
    std::expected<std::vector<std::uint64_t>, ConsensusError> storage_challenge_indices(
        const ActorId&          network,
        const ActorId&          provider,
        const StorageDataset&   dataset,
        const StorageChallenge& challenge);
    std::expected<StorageProof, ConsensusError> make_storage_proof(const ActorId&           network,
                                                                   const ActorId&           provider,
                                                                   const StorageDataset&    dataset,
                                                                   const StorageChallenge&  challenge,
                                                                   const MerkleValueReader& read_chunk);
    bool                                        verify_storage_proof(const ActorId&          network,
                                                                     const ActorId&          provider,
                                                                     const StorageDataset&   dataset,
                                                                     const StorageChallenge& challenge,
                                                                     const StorageProof&     proof);

} // namespace ExtraChain::Consensus
