#include "consensus/storage_proof.h"

#include "utils/exc_utils.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        bool valid_hash(std::string_view hash) {
            return hash.size() == 64 && std::ranges::all_of(hash, [](char character) {
                       return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
                   });
        }

        bool valid_size(std::uint64_t bytes) {
            return bytes != 0 && bytes <= MaximumStorageDatasetBytes;
        }

        std::uint64_t chunk_count(std::uint64_t bytes) {
            return bytes / StorageChunkBytes + (bytes % StorageChunkBytes != 0);
        }

        std::uint64_t chunk_size(std::uint64_t bytes, std::uint64_t index) {
            return std::min<std::uint64_t>(StorageChunkBytes, bytes - index * StorageChunkBytes);
        }

        std::string chunk_value(std::uint64_t bytes, std::uint64_t index, const std::string& chunk) {
            return "EXC_STORAGE_CHUNK_V1" + MessagePack::serialize(std::tuple { bytes, index, chunk });
        }

        template <typename T>
        std::string hash_value(std::string_view domain, const T& value) {
            return Utils::calculate_hash(std::string(domain) + MessagePack::serialize(value),
                                         Utils::HashAlgorithm::Blake3);
        }
    } // namespace

    std::expected<StorageDataset, ConsensusError> commit_storage_dataset(std::uint64_t            bytes,
                                                                         const MerkleValueReader& read_chunk,
                                                                         const MerkleNodeSink&    sink) {
        if (!valid_size(bytes) || !read_chunk)
            return std::unexpected(ConsensusError::InvalidProof);
        const auto tree = build_merkle_tree(
            chunk_count(bytes),
            [&](std::uint64_t index) -> std::expected<std::string, ConsensusError> {
                const auto chunk = read_chunk(index);
                if (!chunk.has_value())
                    return std::unexpected(chunk.error());
                if (chunk.value().size() != chunk_size(bytes, index))
                    return std::unexpected(ConsensusError::InvalidProof);
                return chunk_value(bytes, index, chunk.value());
            },
            { },
            sink);
        if (!tree.has_value())
            return std::unexpected(tree.error());
        return StorageDataset { .bytes = bytes, .root = tree.value().root };
    }

    std::expected<std::string, ConsensusError> storage_dataset_id(const ActorId&        network,
                                                                  const StorageDataset& dataset) {
        if (network.is_zero() || dataset.version != 1 || !valid_size(dataset.bytes) || !valid_hash(dataset.root))
            return std::unexpected(ConsensusError::InvalidProof);
        return hash_value("EXC_STORAGE_DATASET_V1", std::tuple { network.to_string(), dataset });
    }

    std::expected<std::vector<std::uint64_t>, ConsensusError> storage_challenge_indices(
        const ActorId&          network,
        const ActorId&          provider,
        const StorageDataset&   dataset,
        const StorageChallenge& challenge) {
        const auto identity = storage_dataset_id(network, dataset);
        if (!identity.has_value() || provider.is_zero() || !valid_hash(challenge.checkpoint))
            return std::unexpected(ConsensusError::InvalidProof);
        const auto seed    = hash_value("EXC_STORAGE_CHALLENGE_V1",
                                        std::tuple { identity.value(), provider.to_string(), challenge });
        const auto count   = chunk_count(dataset.bytes);
        const auto samples = std::min<std::uint64_t>(count, StorageChallengeSamples);
        std::set<std::uint64_t> selected;
        std::uint64_t           nonce = 0;
        for (auto index = count - samples; index < count; ++index) {
            const auto                   width     = index + 1;
            const auto                   threshold = (std::uint64_t(0) - width) % width;
            std::optional<std::uint64_t> chosen;
            for (std::size_t attempt = 0; attempt < 128; ++attempt) {
                const auto    hash   = hash_value("EXC_STORAGE_SAMPLE_V1", std::tuple { seed, nonce++ });
                std::uint64_t value  = 0;
                const auto    parsed = std::from_chars(hash.data(), hash.data() + 16, value, 16);
                if (parsed.ec != std::errc { } || parsed.ptr != hash.data() + 16)
                    return std::unexpected(ConsensusError::InvalidProof);
                if (value >= threshold) {
                    chosen = value % width;
                    break;
                }
            }
            if (!chosen.has_value())
                return std::unexpected(ConsensusError::InvalidProof);
            selected.insert(selected.contains(chosen.value()) ? index : chosen.value());
        }
        return std::vector<std::uint64_t>(selected.begin(), selected.end());
    }

    std::expected<StorageProof, ConsensusError> make_storage_proof(const ActorId&           network,
                                                                   const ActorId&           provider,
                                                                   const StorageDataset&    dataset,
                                                                   const StorageChallenge&  challenge,
                                                                   const MerkleValueReader& read_chunk) {
        const auto targets = storage_challenge_indices(network, provider, dataset, challenge);
        if (!targets.has_value() || !read_chunk)
            return std::unexpected(ConsensusError::InvalidProof);
        StorageProof result;
        const auto   tree = build_merkle_tree(
            chunk_count(dataset.bytes),
            [&](std::uint64_t index) -> std::expected<std::string, ConsensusError> {
                auto chunk = read_chunk(index);
                if (!chunk.has_value())
                    return std::unexpected(chunk.error());
                if (chunk.value().size() != chunk_size(dataset.bytes, index))
                    return std::unexpected(ConsensusError::InvalidProof);
                const auto value = chunk_value(dataset.bytes, index, chunk.value());
                if (std::ranges::binary_search(targets.value(), index))
                    result.samples.push_back(StorageSample { .bytes = std::move(chunk.value()) });
                return value;
            },
            targets.value());
        if (!tree.has_value())
            return std::unexpected(tree.error());
        if (tree.value().root != dataset.root)
            return std::unexpected(ConsensusError::InvalidRoot);
        for (std::size_t index = 0; index < result.samples.size(); ++index)
            result.samples[index].path = tree.value().proofs[index];
        return result;
    }

    std::expected<StorageProof, ConsensusError> make_indexed_storage_proof(const ActorId&           network,
                                                                           const ActorId&           provider,
                                                                           const StorageDataset&    dataset,
                                                                           const StorageChallenge&  challenge,
                                                                           const MerkleValueReader& read_chunk,
                                                                           const MerkleNodeReader&  read_node) {
        const auto targets = storage_challenge_indices(network, provider, dataset, challenge);
        if (!targets.has_value() || !read_chunk || !read_node)
            return std::unexpected(ConsensusError::InvalidProof);
        StorageProof result;
        for (auto target : targets.value()) {
            auto chunk = read_chunk(target);
            if (!chunk.has_value())
                return std::unexpected(chunk.error());
            if (chunk.value().size() != chunk_size(dataset.bytes, target))
                return std::unexpected(ConsensusError::InvalidProof);
            auto path = make_indexed_merkle_proof(chunk_count(dataset.bytes), target, read_node);
            if (!path.has_value())
                return std::unexpected(path.error());
            result.samples.push_back(
                StorageSample { .bytes = std::move(chunk.value()), .path = std::move(path.value()) });
        }
        // A persisted index is a cache. Check it against the registered root and current sampled bytes.
        if (!verify_storage_proof(network, provider, dataset, challenge, result))
            return std::unexpected(ConsensusError::InvalidProof);
        return result;
    }

    bool verify_storage_proof(const ActorId&          network,
                              const ActorId&          provider,
                              const StorageDataset&   dataset,
                              const StorageChallenge& challenge,
                              const StorageProof&     proof) {
        if (proof.samples.empty() || proof.samples.size() > StorageChallengeSamples)
            return false;
        const auto targets = storage_challenge_indices(network, provider, dataset, challenge);
        if (!targets.has_value() || proof.samples.size() != targets.value().size())
            return false;
        for (std::size_t index = 0; index < proof.samples.size(); ++index) {
            const auto& sample = proof.samples[index];
            const auto  target = targets.value()[index];
            if (sample.path.leaf_index != target || sample.path.leaf_count != chunk_count(dataset.bytes)
                || sample.bytes.size() != chunk_size(dataset.bytes, target)
                || !verify_merkle_proof(chunk_value(dataset.bytes, target, sample.bytes),
                                        sample.path,
                                        dataset.root))
                return false;
        }
        return true;
    }
} // namespace ExtraChain::Consensus
