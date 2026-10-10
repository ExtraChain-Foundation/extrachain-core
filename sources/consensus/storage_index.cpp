#include "consensus/storage_index.h"

#include "utils/file_io.h"

#include <bit>
#include <fstream>
#include <map>

namespace ExtraChain::Consensus {
    namespace {
        // V2 keeps BLAKE3 chaining values; a V1 index of the older Merkle tree is rebuilt.
        constexpr std::string_view IndexMagic = "EXC_STORAGE_INDEX_V2\n";
        constexpr std::uint64_t    HashBytes  = 64;

        std::string index_header(std::uint64_t bytes) {
            return std::string(IndexMagic) + fmt::format("{:016x}", bytes);
        }

        // An index file checked against its dataset. Complete subtrees sit in postorder, without the
        // virtual nodes of odd-leaf padding.
        class IndexReader {
        public:
            static std::expected<IndexReader, ConsensusError> open(const std::filesystem::path& path,
                                                                   const StorageDataset&        dataset) {
                IndexReader reader;
                reader.leaves = dataset.bytes / StorageChunkBytes + (dataset.bytes % StorageChunkBytes != 0);
                reader.nodes  = 2 * reader.leaves - std::popcount(reader.leaves);
                const auto header = index_header(dataset.bytes);
                reader.header_size = header.size();
                reader.length      = header.size() + (reader.nodes + 1) * HashBytes;
                reader.file.open(path, std::ios::binary | std::ios::ate);
                if (!reader.file || reader.file.tellg() != static_cast<std::streamoff>(reader.length))
                    return std::unexpected(ConsensusError::StorageFailure);
                const auto actual_header = reader.read(0, header.size());
                const auto root          = reader.read(reader.length - HashBytes, HashBytes);
                if (!actual_header.has_value() || actual_header.value() != header || !root.has_value()
                    || root.value() != dataset.root)
                    return std::unexpected(ConsensusError::InvalidProof);
                return reader;
            }

            std::expected<std::string, ConsensusError> node(std::uint64_t begin, std::uint32_t height) {
                if (height > 32 || begin >= leaves)
                    return std::unexpected(ConsensusError::InvalidProof);
                const auto span = std::uint64_t(1) << height;
                if (begin % span != 0 || span > leaves - begin)
                    return std::unexpected(ConsensusError::InvalidProof);
                const auto end    = begin + span;
                const auto offset = 2 * end - std::popcount(end) - (std::countr_zero(end) - height) - 1;
                if (offset >= nodes)
                    return std::unexpected(ConsensusError::InvalidProof);
                return read(header_size + offset * HashBytes, HashBytes);
            }

        private:
            std::expected<std::string, ConsensusError> read(std::uint64_t offset, std::uint64_t size) {
                if (offset > length || size > length - offset)
                    return std::unexpected(ConsensusError::InvalidProof);
                file.seekg(static_cast<std::streamoff>(offset));
                std::string value(size, '\0');
                if (!file.read(value.data(), static_cast<std::streamsize>(size)))
                    return std::unexpected(ConsensusError::StorageFailure);
                return value;
            }

            std::ifstream file;
            std::uint64_t leaves      = 0;
            std::uint64_t nodes       = 0;
            std::uint64_t header_size = 0;
            std::uint64_t length      = 0;
        };
    } // namespace

    std::expected<StorageDataset, ConsensusError> write_storage_index(const std::filesystem::path& path,
                                                                      std::uint64_t                bytes,
                                                                      const MerkleValueReader&     read_chunk) {
        if (bytes == 0 || bytes > MaximumStorageDatasetBytes || !read_chunk)
            return std::unexpected(ConsensusError::InvalidProof);
        std::expected<StorageDataset, ConsensusError> dataset = std::unexpected(ConsensusError::StorageFailure);
        const auto stored = FileIo::write_private_atomic_stream(path, [&](std::FILE* file) {
            const auto header = index_header(bytes);
            if (std::fwrite(header.data(), 1, header.size(), file) != header.size())
                return false;
            dataset = commit_storage_dataset(bytes, read_chunk, [&](auto, auto, std::string_view hash) {
                return hash.size() == HashBytes && std::fwrite(hash.data(), 1, hash.size(), file) == hash.size();
            });
            return dataset.has_value()
                   && std::fwrite(dataset.value().root.data(), 1, HashBytes, file) == HashBytes;
        });
        if (!dataset.has_value())
            return std::unexpected(dataset.error());
        if (!stored.has_value())
            return std::unexpected(ConsensusError::StorageFailure);
        return dataset;
    }

    std::expected<StorageProof, ConsensusError> make_storage_proof_from_index(
        const std::filesystem::path& path,
        const ActorId&               network,
        const ActorId&               provider,
        const StorageDataset&        dataset,
        const StorageChallenge&      challenge,
        const MerkleValueReader&     read_chunk) {
        if (!read_chunk || !storage_dataset_id(network, dataset).has_value())
            return std::unexpected(ConsensusError::InvalidProof);
        // Checked up front: a single chunk is its own root and would never read a node.
        auto reader = IndexReader::open(path, dataset);
        if (!reader.has_value())
            return std::unexpected(reader.error());
        return make_provider_storage_proof(
            network,
            provider,
            std::span(&dataset, 1),
            challenge,
            [&](std::size_t, std::uint64_t chunk) {
                return read_chunk(chunk);
            },
            [&](std::size_t, std::uint64_t begin, std::uint32_t height) {
                return reader.value().node(begin, height);
            });
    }

    std::expected<StorageProof, ConsensusError> make_provider_proof_from_indexes(
        std::span<const std::filesystem::path> paths,
        const ActorId&                         network,
        const ActorId&                         provider,
        std::span<const StorageDataset>        datasets,
        const StorageChallenge&                challenge,
        const StorageChunkSource&              read_chunk) {
        if (paths.size() != datasets.size() || !read_chunk)
            return std::unexpected(ConsensusError::InvalidProof);
        for (const auto& dataset : datasets)
            if (!storage_dataset_id(network, dataset).has_value())
                return std::unexpected(ConsensusError::InvalidProof);
        // Only the datasets a sample falls in are opened.
        std::map<std::size_t, IndexReader> readers;
        return make_provider_storage_proof(
            network,
            provider,
            datasets,
            challenge,
            read_chunk,
            [&](std::size_t dataset, std::uint64_t begin, std::uint32_t height)
                -> std::expected<std::string, ConsensusError> {
                if (dataset >= datasets.size())
                    return std::unexpected(ConsensusError::InvalidProof);
                auto reader = readers.find(dataset);
                if (reader == readers.end()) {
                    auto opened = IndexReader::open(paths[dataset], datasets[dataset]);
                    if (!opened.has_value())
                        return std::unexpected(opened.error());
                    reader = readers.emplace(dataset, std::move(opened.value())).first;
                }
                return reader->second.node(begin, height);
            });
    }
} // namespace ExtraChain::Consensus
