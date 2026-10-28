#include "consensus/storage_proof.h"
#include "test_support.h"
#include "utils/hash.h"
#include "utils/serialization.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

using namespace ExtraChain::Consensus;

int main() {
    const auto             network  = ActorId::create(std::string(40, '1')).value();
    const auto             provider = ActorId::create(std::string(40, '2')).value();
    const StorageChallenge challenge { .epoch = 7, .checkpoint = std::string(64, 'a') };
    for (const std::uint64_t size : { 1ULL, 16384ULL, 16385ULL, 49152ULL, 131077ULL, 4210711ULL }) {
        const auto reader = [size](std::uint64_t index) -> std::expected<std::string, ConsensusError> {
            return std::string(std::min<std::uint64_t>(StorageChunkBytes, size - index * StorageChunkBytes),
                               static_cast<char>(index % 251));
        };
        const auto dataset = commit_storage_dataset(size, reader);
        TEST_REQUIRE(dataset.has_value());
        // The root is the plain BLAKE3 hash of the bytes, the hash a DFS row signs.
        std::string content;
        for (std::uint64_t index = 0; index * StorageChunkBytes < size; ++index)
            content += reader(index).value();
        TEST_REQUIRE_EQ(dataset.value().root, Utils::calculate_hash(content));
        const auto identity = storage_dataset_id(network, dataset.value());
        TEST_REQUIRE(identity.has_value());
        TEST_REQUIRE_EQ(identity, storage_dataset_id(network, commit_storage_dataset(size, reader).value()));
        const auto proof = make_storage_proof(network, provider, dataset.value(), challenge, reader);
        TEST_REQUIRE(proof.has_value());
        TEST_REQUIRE(verify_storage_proof(network, provider, dataset.value(), challenge, proof.value()));
        TEST_REQUIRE(MessagePack::serialize(proof.value()).size() < 160 * 1024);
        TEST_REQUIRE_EQ(MessagePack::serialize(proof.value()),
                        MessagePack::serialize(
                            make_storage_proof(network, provider, dataset.value(), challenge, reader).value()));

        auto damaged = proof.value();
        damaged.samples.front().bytes[0] ^= 1;
        TEST_REQUIRE(!verify_storage_proof(network, provider, dataset.value(), challenge, damaged));
        damaged = proof.value();
        damaged.samples.front().bytes.push_back('x');
        TEST_REQUIRE(!verify_storage_proof(network, provider, dataset.value(), challenge, damaged));
        damaged = proof.value();
        damaged.samples.front().path.leaf_count += 1;
        TEST_REQUIRE(!verify_storage_proof(network, provider, dataset.value(), challenge, damaged));
        damaged = proof.value();
        damaged.samples.pop_back();
        TEST_REQUIRE(!verify_storage_proof(network, provider, dataset.value(), challenge, damaged));
        damaged = proof.value();
        damaged.samples.push_back(damaged.samples.front());
        TEST_REQUIRE(!verify_storage_proof(network, provider, dataset.value(), challenge, damaged));
        if (proof.value().samples.size() > 1) {
            damaged = proof.value();
            std::swap(damaged.samples[0], damaged.samples[1]);
            TEST_REQUIRE(!verify_storage_proof(network, provider, dataset.value(), challenge, damaged));
        }
        auto wrong_dataset = dataset.value();
        wrong_dataset.bytes += 1;
        TEST_REQUIRE(!verify_storage_proof(network, provider, wrong_dataset, challenge, proof.value()));
        wrong_dataset         = dataset.value();
        wrong_dataset.root[0] = wrong_dataset.root[0] == '0' ? '1' : '0';
        TEST_REQUIRE(!verify_storage_proof(network, provider, wrong_dataset, challenge, proof.value()));
        TEST_REQUIRE(!make_storage_proof(network, provider, wrong_dataset, challenge, reader).has_value());
        wrong_dataset         = dataset.value();
        wrong_dataset.version = 1;
        TEST_REQUIRE(!storage_dataset_id(network, wrong_dataset).has_value());
        TEST_REQUIRE(!verify_storage_proof(network, provider, wrong_dataset, challenge, proof.value()));

        if (size > 4 * 1024 * 1024) {
            const auto targets = storage_challenge_indices(network, provider, dataset.value(), challenge).value();
            TEST_REQUIRE_EQ(targets.size(), StorageChallengeSamples);
            TEST_REQUIRE(std::ranges::adjacent_find(targets) == targets.end());
            auto other_challenge = challenge;
            other_challenge.epoch += 1;
            TEST_REQUIRE(
                !verify_storage_proof(network, provider, dataset.value(), other_challenge, proof.value()));
            other_challenge               = challenge;
            other_challenge.checkpoint[0] = 'b';
            TEST_REQUIRE(
                !verify_storage_proof(network, provider, dataset.value(), other_challenge, proof.value()));
            TEST_REQUIRE(!verify_storage_proof(provider, provider, dataset.value(), challenge, proof.value()));
            TEST_REQUIRE(!verify_storage_proof(network, network, dataset.value(), challenge, proof.value()));
        }
        TEST_REQUIRE(!make_storage_proof(network,
                                         provider,
                                         dataset.value(),
                                         challenge,
                                         [](auto) -> std::expected<std::string, ConsensusError> {
                                             return std::unexpected(ConsensusError::DataUnavailable);
                                         })
                          .has_value());
    }
    // One proof for all of a provider's datasets, drawn from their chunks together and built from indexes.
    {
        using Index = std::map<std::pair<std::uint64_t, std::uint32_t>, std::string>;
        std::vector<StorageDataset> datasets;
        std::vector<Index>          indexes;
        const auto content = [](std::size_t dataset, std::uint64_t size, std::uint64_t index) {
            return std::string(std::min<std::uint64_t>(StorageChunkBytes, size - index * StorageChunkBytes),
                               static_cast<char>((index * 7 + dataset) % 251));
        };
        const std::vector<std::uint64_t> sizes { 700ULL, 5000ULL, 40000ULL };
        for (std::size_t dataset = 0; dataset < sizes.size(); ++dataset) {
            auto& index = indexes.emplace_back();
            datasets.push_back(commit_storage_dataset(
                                   sizes[dataset],
                                   [&](std::uint64_t chunk) -> std::expected<std::string, ConsensusError> {
                                       return content(dataset, sizes[dataset], chunk);
                                   },
                                   [&](std::uint64_t begin, std::uint32_t height, std::string_view value) {
                                       return index.try_emplace({ begin, height }, value).second;
                                   })
                                   .value());
        }
        const StorageChunkSource chunks = [&](std::size_t dataset, std::uint64_t chunk)
            -> std::expected<std::string, ConsensusError> { return content(dataset, sizes[dataset], chunk); };
        const StorageNodeSource nodes = [&](std::size_t dataset, std::uint64_t begin, std::uint32_t height)
            -> std::expected<std::string, ConsensusError> {
            const auto found = indexes[dataset].find({ begin, height });
            if (found == indexes[dataset].end())
                return std::unexpected(ConsensusError::DataUnavailable);
            return found->second;
        };
        const auto targets = storage_provider_targets(network, provider, datasets, challenge).value();
        TEST_REQUIRE_EQ(targets.size(), StorageChallengeSamples);
        std::set<std::size_t> touched;
        for (const auto& target : targets)
            touched.insert(target.dataset);
        TEST_REQUIRE(touched.size() > 1);
        const auto proof = make_provider_storage_proof(network, provider, datasets, challenge, chunks, nodes);
        TEST_REQUIRE(proof.has_value());
        TEST_REQUIRE(verify_provider_storage_proof(network, provider, datasets, challenge, proof.value()));
        auto damaged = proof.value();
        damaged.samples.back().bytes[0] ^= 1;
        TEST_REQUIRE(!verify_provider_storage_proof(network, provider, datasets, challenge, damaged));
        // The seed binds the whole set: a subset or another order asks for other chunks.
        const std::vector<StorageDataset> subset(datasets.begin(), datasets.end() - 1);
        TEST_REQUIRE(!verify_provider_storage_proof(network, provider, subset, challenge, proof.value()));
        auto reordered = datasets;
        std::swap(reordered.front(), reordered.back());
        TEST_REQUIRE(!verify_provider_storage_proof(network, provider, reordered, challenge, proof.value()));
        TEST_REQUIRE(!verify_provider_storage_proof(network, network, datasets, challenge, proof.value()));
        TEST_REQUIRE(!make_provider_storage_proof(network,
                                                  provider,
                                                  datasets,
                                                  challenge,
                                                  chunks,
                                                  [](std::size_t, std::uint64_t, std::uint32_t)
                                                      -> std::expected<std::string, ConsensusError> {
                                                      return std::string(64, 'a');
                                                  })
                          .has_value());
        TEST_REQUIRE(!storage_provider_targets(network, provider, { }, challenge).has_value());
    }

    const auto empty = [](auto) -> std::expected<std::string, ConsensusError> {
        return "";
    };
    TEST_REQUIRE(!commit_storage_dataset(0, empty).has_value());
    TEST_REQUIRE(!commit_storage_dataset(1, empty).has_value());
    TEST_REQUIRE(!commit_storage_dataset(MaximumStorageDatasetBytes + 1, empty).has_value());
    TEST_REQUIRE(!commit_storage_dataset(1, { }).has_value());
    const StorageDataset maximum { .bytes = MaximumStorageDatasetBytes, .root = std::string(64, 'a') };
    const auto           targets = storage_challenge_indices(network, provider, maximum, challenge);
    TEST_REQUIRE(targets.has_value());
    TEST_REQUIRE_EQ(targets.value().size(), StorageChallengeSamples);
    TEST_REQUIRE(targets.value().back() < (std::uint64_t(1) << 32));
    TEST_REQUIRE(!storage_challenge_indices(ActorId(), provider, maximum, challenge).has_value());
    TEST_REQUIRE(!storage_challenge_indices(network, ActorId(), maximum, challenge).has_value());
    TEST_REQUIRE(!storage_challenge_indices(network, provider, maximum, StorageChallenge { }).has_value());
}
