#include "consensus/storage_proof.h"
#include "test_support.h"
#include "utils/serialization.h"

#include <algorithm>
#include <limits>

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
        wrong_dataset.version = 2;
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
