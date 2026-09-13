#include "consensus/storage_index.h"

#include "utils/file_io.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view IndexMagic = "EXC_STORAGE_INDEX_V1\n";
        constexpr std::uint64_t    HashBytes  = 64;

        std::string index_header(std::uint64_t bytes) {
            return std::string(IndexMagic) + fmt::format("{:016x}", bytes);
        }
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
        if (!storage_dataset_id(network, dataset).has_value())
            return std::unexpected(ConsensusError::InvalidProof);
        const auto    leaves = dataset.bytes / StorageChunkBytes + (dataset.bytes % StorageChunkBytes != 0);
        const auto    nodes  = 2 * leaves - std::popcount(leaves);
        const auto    header = index_header(dataset.bytes);
        const auto    length = header.size() + (nodes + 1) * HashBytes;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() != static_cast<std::streamoff>(length))
            return std::unexpected(ConsensusError::StorageFailure);
        const auto read = [&](std::uint64_t offset,
                              std::uint64_t size) -> std::expected<std::string, ConsensusError> {
            if (offset > length || size > length - offset)
                return std::unexpected(ConsensusError::InvalidProof);
            file.seekg(static_cast<std::streamoff>(offset));
            std::string value(size, '\0');
            if (!file.read(value.data(), static_cast<std::streamsize>(size)))
                return std::unexpected(ConsensusError::StorageFailure);
            return value;
        };
        const auto actual_header = read(0, header.size());
        const auto root          = read(length - HashBytes, HashBytes);
        if (!actual_header.has_value() || actual_header.value() != header || !root.has_value()
            || root.value() != dataset.root)
            return std::unexpected(ConsensusError::InvalidProof);
        return make_indexed_storage_proof(network,
                                          provider,
                                          dataset,
                                          challenge,
                                          read_chunk,
                                          [&](std::uint64_t begin,
                                              std::uint32_t height) -> std::expected<std::string, ConsensusError> {
                                              if (height > 32 || begin >= leaves)
                                                  return std::unexpected(ConsensusError::InvalidProof);
                                              const auto span = std::uint64_t(1) << height;
                                              if (begin % span != 0 || span > leaves - begin)
                                                  return std::unexpected(ConsensusError::InvalidProof);
                                              const auto end = begin + span;
                                              // Complete subtrees are emitted in postorder. Omit virtual nodes
                                              // from odd-leaf padding.
                                              const auto offset = 2 * end - std::popcount(end)
                                                                  - (std::countr_zero(end) - height) - 1;
                                              if (offset >= nodes)
                                                  return std::unexpected(ConsensusError::InvalidProof);
                                              return read(header.size() + offset * HashBytes, HashBytes);
                                          });
    }
} // namespace ExtraChain::Consensus
