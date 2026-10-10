#include "consensus/storage_proof.h"

#include "utils/blake3_tree.h"
#include "utils/exc_utils.h"
#include "utils/serialization.h"

#include <blake3.h>

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

        using Utils::Blake3Tree::ChainingValue;

        std::string hex(const ChainingValue& value) {
            static constexpr char Digits[] = "0123456789abcdef";
            std::string           result;
            for (const auto byte : value) {
                result.push_back(Digits[byte >> 4]);
                result.push_back(Digits[byte & 15]);
            }
            return result;
        }

        std::optional<ChainingValue> chaining_value(std::string_view text) {
            if (!valid_hash(text))
                return std::nullopt;
            ChainingValue result;
            for (std::size_t index = 0; index < result.size(); ++index) {
                const auto digit = [](char value) {
                    return static_cast<std::uint8_t>(value <= '9' ? value - '0' : value - 'a' + 10);
                };
                result[index] =
                    static_cast<std::uint8_t>(digit(text[2 * index]) << 4 | digit(text[2 * index + 1]));
            }
            return result;
        }

        std::optional<std::vector<ChainingValue>> sample_path(const MerkleProof& path) {
            std::vector<ChainingValue> result;
            for (const auto& sibling : path.siblings) {
                const auto value = chaining_value(sibling);
                if (!value.has_value())
                    return std::nullopt;
                result.push_back(value.value());
            }
            return result;
        }

        MerkleProof proof_path(std::uint64_t count, std::uint64_t index, const std::vector<ChainingValue>& path) {
            MerkleProof result { .leaf_index = index, .leaf_count = count };
            for (const auto& sibling : path)
                result.siblings.push_back(hex(sibling));
            return result;
        }

        template <typename T>
        std::string hash_value(std::string_view domain, const T& value) {
            return Utils::calculate_hash(std::string(domain) + MessagePack::serialize(value),
                                         Utils::HashAlgorithm::Blake3);
        }

        // Distinct indices in [0, count), at most StorageChallengeSamples of them, drawn from the seed.
        std::expected<std::vector<std::uint64_t>, ConsensusError> sample_indices(const std::string& seed,
                                                                                 std::uint64_t      count) {
            const auto              samples = std::min<std::uint64_t>(count, StorageChallengeSamples);
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
    } // namespace

    std::expected<StorageDataset, ConsensusError> commit_storage_dataset(std::uint64_t            bytes,
                                                                         const MerkleValueReader& read_chunk,
                                                                         const MerkleNodeSink&    sink) {
        if (!valid_size(bytes) || !read_chunk)
            return std::unexpected(ConsensusError::InvalidProof);
        // One pass: the library hash is the root a DFS row signs, and the chaining values of every full
        // block go to the index in postorder, the order the index reader expects.
        struct Node {
            std::uint64_t begin  = 0;
            std::uint32_t height = 0;
            ChainingValue value;
        };
        std::vector<Node> stack;
        blake3_hasher     hasher;
        blake3_hasher_init(&hasher);
        for (std::uint64_t index = 0; index < chunk_count(bytes); ++index) {
            const auto chunk = read_chunk(index);
            if (!chunk.has_value())
                return std::unexpected(chunk.error());
            if (chunk.value().size() != chunk_size(bytes, index))
                return std::unexpected(ConsensusError::InvalidProof);
            blake3_hasher_update(&hasher, chunk.value().data(), chunk.value().size());
            Node node { .begin = index, .value = Utils::Blake3Tree::chunk_value(chunk.value(), index) };
            for (;;) {
                if (sink && !sink(node.begin, node.height, hex(node.value)))
                    return std::unexpected(ConsensusError::StorageFailure);
                if (stack.empty() || stack.back().height != node.height)
                    break;
                node = Node { .begin  = stack.back().begin,
                              .height = node.height + 1,
                              .value  = Utils::Blake3Tree::parent_value(stack.back().value, node.value) };
                stack.pop_back();
            }
            stack.push_back(node);
        }
        ChainingValue root;
        blake3_hasher_finalize(&hasher, root.data(), root.size());
        return StorageDataset { .bytes = bytes, .root = hex(root) };
    }

    std::expected<std::string, ConsensusError> storage_dataset_id(const ActorId&        network,
                                                                  const StorageDataset& dataset) {
        if (network.is_zero() || dataset.version != StorageDatasetVersion || !valid_size(dataset.bytes)
            || !valid_hash(dataset.root))
            return std::unexpected(ConsensusError::InvalidProof);
        return hash_value("EXC_STORAGE_DATASET_V1", std::tuple { network.to_string(), dataset });
    }

    std::expected<std::vector<std::uint64_t>, ConsensusError> storage_challenge_indices(
        const ActorId&          network,
        const ActorId&          provider,
        const StorageDataset&   dataset,
        const StorageChallenge& challenge) {
        const auto targets = storage_provider_targets(network, provider, std::span(&dataset, 1), challenge);
        if (!targets.has_value())
            return std::unexpected(targets.error());
        std::vector<std::uint64_t> result;
        for (const auto& target : targets.value())
            result.push_back(target.chunk);
        return result;
    }

    std::expected<std::vector<StorageTarget>, ConsensusError> storage_provider_targets(
        const ActorId&                  network,
        const ActorId&                  provider,
        std::span<const StorageDataset> datasets,
        const StorageChallenge&         challenge) {
        if (datasets.empty() || datasets.size() > MaximumStorageProviderDatasets || provider.is_zero()
            || !valid_hash(challenge.checkpoint))
            return std::unexpected(ConsensusError::InvalidProof);
        // The seed binds the provider's whole set, so a proof cannot pick a subset of its datasets.
        std::vector<std::string>   identities;
        std::vector<std::uint64_t> starts;
        std::uint64_t              total = 0;
        for (const auto& dataset : datasets) {
            const auto identity = storage_dataset_id(network, dataset);
            if (!identity.has_value())
                return std::unexpected(identity.error());
            identities.push_back(identity.value());
            starts.push_back(total);
            total += chunk_count(dataset.bytes);
        }
        const auto seed = hash_value("EXC_STORAGE_PROVIDER_CHALLENGE_V1",
                                     std::tuple { provider.to_string(), challenge, identities });
        const auto chosen = sample_indices(seed, total);
        if (!chosen.has_value())
            return std::unexpected(chosen.error());
        std::vector<StorageTarget> result;
        for (const auto index : chosen.value()) {
            const auto dataset =
                static_cast<std::size_t>(std::ranges::upper_bound(starts, index) - starts.begin() - 1);
            result.push_back(StorageTarget { .dataset = dataset, .chunk = index - starts[dataset] });
        }
        return result;
    }

    std::expected<StorageProof, ConsensusError> make_storage_proof(const ActorId&           network,
                                                                   const ActorId&           provider,
                                                                   const StorageDataset&    dataset,
                                                                   const StorageChallenge&  challenge,
                                                                   const MerkleValueReader& read_chunk) {
        const auto targets = storage_challenge_indices(network, provider, dataset, challenge);
        if (!targets.has_value() || !read_chunk)
            return std::unexpected(ConsensusError::InvalidProof);
        const auto                 count = chunk_count(dataset.bytes);
        std::vector<ChainingValue> values;
        StorageProof               result;
        blake3_hasher              hasher;
        blake3_hasher_init(&hasher);
        for (std::uint64_t index = 0; index < count; ++index) {
            auto chunk = read_chunk(index);
            if (!chunk.has_value())
                return std::unexpected(chunk.error());
            if (chunk.value().size() != chunk_size(dataset.bytes, index))
                return std::unexpected(ConsensusError::InvalidProof);
            blake3_hasher_update(&hasher, chunk.value().data(), chunk.value().size());
            values.push_back(Utils::Blake3Tree::chunk_value(chunk.value(), index));
            if (std::ranges::binary_search(targets.value(), index))
                result.samples.push_back(StorageSample { .bytes = std::move(chunk.value()) });
        }
        ChainingValue root;
        blake3_hasher_finalize(&hasher, root.data(), root.size());
        if (hex(root) != dataset.root)
            return std::unexpected(ConsensusError::InvalidRoot);
        for (std::size_t index = 0; index < result.samples.size(); ++index) {
            const auto target          = targets.value()[index];
            result.samples[index].path = proof_path(count,
                                                    target,
                                                    count == 1 ? std::vector<ChainingValue> {}
                                                               : Utils::Blake3Tree::chunk_path(values, target));
        }
        return result;
    }

    std::expected<StorageProof, ConsensusError> make_provider_storage_proof(
        const ActorId&                  network,
        const ActorId&                  provider,
        std::span<const StorageDataset> datasets,
        const StorageChallenge&         challenge,
        const StorageChunkSource&       read_chunk,
        const StorageNodeSource&        read_node) {
        const auto targets = storage_provider_targets(network, provider, datasets, challenge);
        if (!targets.has_value() || !read_chunk || !read_node)
            return std::unexpected(ConsensusError::InvalidProof);
        StorageProof result;
        for (const auto& target : targets.value()) {
            const auto& dataset = datasets[target.dataset];
            const auto  count   = chunk_count(dataset.bytes);
            auto        chunk   = read_chunk(target.dataset, target.chunk);
            if (!chunk.has_value())
                return std::unexpected(chunk.error());
            if (chunk.value().size() != chunk_size(dataset.bytes, target.chunk))
                return std::unexpected(ConsensusError::InvalidProof);
            std::optional<std::vector<ChainingValue>> path = std::vector<ChainingValue> { };
            if (count > 1)
                path = Utils::Blake3Tree::chunk_path(count,
                                                     target.chunk,
                                                     [&](std::uint32_t level, std::uint64_t block) {
                                                         const auto node =
                                                             read_node(target.dataset, block << level, level);
                                                         return node.has_value() ? chaining_value(node.value())
                                                                                 : std::nullopt;
                                                     });
            if (!path.has_value())
                return std::unexpected(ConsensusError::StorageFailure);
            result.samples.push_back(StorageSample { .bytes = std::move(chunk.value()),
                                                     .path  = proof_path(count, target.chunk, path.value()) });
        }
        // A persisted index is a cache. Check it against the registered roots and current sampled bytes.
        if (!verify_provider_storage_proof(network, provider, datasets, challenge, result))
            return std::unexpected(ConsensusError::InvalidProof);
        return result;
    }

    std::expected<StorageProof, ConsensusError> make_indexed_storage_proof(const ActorId&           network,
                                                                           const ActorId&           provider,
                                                                           const StorageDataset&    dataset,
                                                                           const StorageChallenge&  challenge,
                                                                           const MerkleValueReader& read_chunk,
                                                                           const MerkleNodeReader&  read_node) {
        if (!read_chunk || !read_node)
            return std::unexpected(ConsensusError::InvalidProof);
        return make_provider_storage_proof(
            network,
            provider,
            std::span(&dataset, 1),
            challenge,
            [&](std::size_t, std::uint64_t chunk) {
                return read_chunk(chunk);
            },
            [&](std::size_t, std::uint64_t begin, std::uint32_t height) {
                return read_node(begin, height);
            });
    }

    bool verify_provider_storage_proof(const ActorId&                  network,
                                       const ActorId&                  provider,
                                       std::span<const StorageDataset> datasets,
                                       const StorageChallenge&         challenge,
                                       const StorageProof&             proof) {
        if (proof.samples.empty() || proof.samples.size() > StorageChallengeSamples)
            return false;
        const auto targets = storage_provider_targets(network, provider, datasets, challenge);
        if (!targets.has_value() || proof.samples.size() != targets.value().size())
            return false;
        for (std::size_t index = 0; index < proof.samples.size(); ++index) {
            const auto& sample  = proof.samples[index];
            const auto& target  = targets.value()[index];
            const auto& dataset = datasets[target.dataset];
            const auto  path    = sample_path(sample.path);
            if (sample.path.leaf_index != target.chunk || sample.path.leaf_count != chunk_count(dataset.bytes)
                || !path.has_value()
                || !Utils::Blake3Tree::verify_chunk(
                    sample.bytes, target.chunk, dataset.bytes, path.value(), dataset.root))
                return false;
        }
        return true;
    }

    bool verify_storage_proof(const ActorId&          network,
                              const ActorId&          provider,
                              const StorageDataset&   dataset,
                              const StorageChallenge& challenge,
                              const StorageProof&     proof) {
        return verify_provider_storage_proof(network, provider, std::span(&dataset, 1), challenge, proof);
    }
} // namespace ExtraChain::Consensus
