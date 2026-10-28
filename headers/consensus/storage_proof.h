#pragma once

#include "consensus/consensus_protocol.h"

#include <span>

namespace ExtraChain::Consensus {

    // A proof carries StorageChallengeSamples chunks with their Merkle paths and goes to every
    // node every epoch. 8 chunks of 16 KiB made it 180 KB once in an intent; 16 chunks of 1 KiB
    // make it 38 KB and catch a missing part of a dataset with more samples.
    inline constexpr std::uint32_t StorageChunkBytes          = 1024;
    inline constexpr std::size_t   StorageChallengeSamples    = 16;
    inline constexpr std::uint64_t MaximumStorageDatasetBytes = (std::uint64_t(1) << 32) * StorageChunkBytes;

    // Version 2 roots are the plain BLAKE3 hash of the bytes, the hash a DFS row signs, and samples prove
    // themselves through the BLAKE3 tree. Version 1 used a separate Merkle tree over the same chunks.
    inline constexpr std::uint16_t StorageDatasetVersion = 2;

    struct StorageDataset {
        std::uint16_t version = StorageDatasetVersion;
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
                                                                         const MerkleValueReader& read_chunk,
                                                                         const MerkleNodeSink&    sink = { });
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

    // One proof covers every dataset a provider holds in an epoch: StorageChallengeSamples chunks drawn
    // from all of their chunks together, so a provider sends one request per epoch, not one per file. The
    // datasets come in the order the epoch keeps them; the seed binds the whole set.
    inline constexpr std::size_t MaximumStorageProviderDatasets = 1024;

    struct StorageTarget {
        std::size_t   dataset = 0;
        std::uint64_t chunk   = 0;

        auto operator<=>(const StorageTarget&) const = default;
    };

    using StorageChunkSource =
        std::function<std::expected<std::string, ConsensusError>(std::size_t dataset, std::uint64_t chunk)>;
    using StorageNodeSource = std::function<std::expected<std::string, ConsensusError>(
        std::size_t dataset, std::uint64_t begin, std::uint32_t height)>;

    std::expected<std::vector<StorageTarget>, ConsensusError> storage_provider_targets(
        const ActorId&                  network,
        const ActorId&                  provider,
        std::span<const StorageDataset> datasets,
        const StorageChallenge&         challenge);
    std::expected<StorageProof, ConsensusError> make_provider_storage_proof(
        const ActorId&                  network,
        const ActorId&                  provider,
        std::span<const StorageDataset> datasets,
        const StorageChallenge&         challenge,
        const StorageChunkSource&       read_chunk,
        const StorageNodeSource&        read_node);
    // The same proof without an index: every chunk of a sampled dataset is read to rebuild its tree.
    std::expected<StorageProof, ConsensusError> make_provider_storage_proof(
        const ActorId&                  network,
        const ActorId&                  provider,
        std::span<const StorageDataset> datasets,
        const StorageChallenge&         challenge,
        const StorageChunkSource&       read_chunk);
    bool verify_provider_storage_proof(const ActorId&                  network,
                                       const ActorId&                  provider,
                                       std::span<const StorageDataset> datasets,
                                       const StorageChallenge&         challenge,
                                       const StorageProof&             proof);

    std::expected<StorageProof, ConsensusError> make_indexed_storage_proof(const ActorId&           network,
                                                                           const ActorId&           provider,
                                                                           const StorageDataset&    dataset,
                                                                           const StorageChallenge&  challenge,
                                                                           const MerkleValueReader& read_chunk,
                                                                           const MerkleNodeReader&  read_node);

} // namespace ExtraChain::Consensus
