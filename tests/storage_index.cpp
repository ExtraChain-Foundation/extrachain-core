#include "consensus/storage_index.h"
#include "test_support.h"
#include "utils/exc_utils.h"
#include "utils/file_io.h"
#include "utils/serialization.h"

#include <filesystem>
#include <limits>

using namespace ExtraChain::Consensus;

int main() {
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-storage-index-" + Utils::generate_random_hex(12));
    TEST_REQUIRE(std::filesystem::create_directory(directory));
    const auto             network  = ActorId::create(std::string(40, '1')).value();
    const auto             provider = ActorId::create(std::string(40, '2')).value();
    const StorageChallenge challenge { .epoch = 7, .checkpoint = std::string(64, 'a') };
    for (const std::uint64_t size : { 1ULL, 16384ULL, 16385ULL, 49152ULL, 131077ULL, 4210711ULL }) {
        const auto  path   = directory / std::to_string(size);
        std::size_t reads  = 0;
        const auto  reader = [&](std::uint64_t index) -> std::expected<std::string, ConsensusError> {
            ++reads;
            TEST_REQUIRE(index * StorageChunkBytes < size);
            return std::string(std::min<std::uint64_t>(StorageChunkBytes, size - index * StorageChunkBytes),
                               static_cast<char>(index % 251));
        };
        const auto dataset = write_storage_index(path, size, reader);
        TEST_REQUIRE(dataset.has_value());
        TEST_REQUIRE_EQ(reads, size / StorageChunkBytes + (size % StorageChunkBytes != 0));
        const auto expected = make_storage_proof(network, provider, dataset.value(), challenge, reader).value();
        reads               = 0;
        const auto actual =
            make_storage_proof_from_index(path, network, provider, dataset.value(), challenge, reader);
        TEST_REQUIRE(actual.has_value());
        TEST_REQUIRE_EQ(reads, expected.samples.size());
        TEST_REQUIRE_EQ(MessagePack::serialize(actual.value()), MessagePack::serialize(expected));
        TEST_REQUIRE(verify_storage_proof(network, provider, dataset.value(), challenge, actual.value()));
#ifndef _WIN32
        const auto permissions = std::filesystem::status(path).permissions();
        TEST_REQUIRE((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all))
                     == std::filesystem::perms::none);
#endif
        const auto original    = FileIo::read_all(path).value();
        const auto unavailable = [](auto) -> std::expected<std::string, ConsensusError> {
            return std::unexpected(ConsensusError::DataUnavailable);
        };
        TEST_REQUIRE(!write_storage_index(path, size, unavailable).has_value());
        TEST_REQUIRE_EQ(FileIo::read_all(path).value(), original);
        TEST_REQUIRE(
            !make_storage_proof_from_index(path, network, provider, dataset.value(), challenge, unavailable)
                 .has_value());
        TEST_REQUIRE(!make_storage_proof_from_index(path,
                                                    network,
                                                    provider,
                                                    dataset.value(),
                                                    challenge,
                                                    [&](auto index) -> std::expected<std::string, ConsensusError> {
                                                        auto bytes = reader(index).value();
                                                        bytes[0] ^= 1;
                                                        return bytes;
                                                    })
                          .has_value());
        auto wrong_dataset    = dataset.value();
        wrong_dataset.root[0] = wrong_dataset.root[0] == 'a' ? 'b' : 'a';
        TEST_REQUIRE(
            !make_storage_proof_from_index(path, network, provider, wrong_dataset, challenge, reader).has_value());
        for (const auto damaged : { original.substr(1), original + "x", std::string(original.size(), '0') }) {
            TEST_REQUIRE(FileIo::write_private_atomic(path, damaged).has_value());
            TEST_REQUIRE(
                !make_storage_proof_from_index(path, network, provider, dataset.value(), challenge, reader)
                     .has_value());
        }
        auto       damaged     = original;
        const auto header_size = std::string_view("EXC_STORAGE_INDEX_V1\n").size() + 16;
        std::fill(damaged.begin() + header_size, damaged.end() - 64, 'a');
        TEST_REQUIRE(FileIo::write_private_atomic(path, damaged).has_value());
        TEST_REQUIRE(!make_storage_proof_from_index(path, network, provider, dataset.value(), challenge, reader)
                          .has_value());
    }
    TEST_REQUIRE(!write_storage_index(directory / "invalid", 0, { }).has_value());
    TEST_REQUIRE(!write_storage_index(directory / "invalid", UINT64_MAX, { }).has_value());
    std::filesystem::remove_all(directory);
}
