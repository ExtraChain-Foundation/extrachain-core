#pragma once

#include "dfs/vector_index.h"

namespace Dfs {
    struct VectorIndexSlice {
        VectorIndexSummary              summary;
        std::vector<VectorIndexSummary> children;
        std::vector<DbRow>              rows;
    };
    BOOST_DESCRIBE_STRUCT(VectorIndexSlice, (), (summary, children, rows))

    class VectorSnapshot {
    public:
        static std::expected<std::unique_ptr<VectorSnapshot>, std::string> open(const FsPath&      path,
                                                                                const std::string& primary);
        ~VectorSnapshot();
        VectorSnapshot(const VectorSnapshot&)            = delete;
        VectorSnapshot& operator=(const VectorSnapshot&) = delete;

        const VectorIndexRoot&                       root() const;
        bool                                         expired(std::chrono::steady_clock::time_point now) const;
        std::expected<VectorIndexSlice, std::string> read(std::string_view prefix);
        static bool                                  verify(const VectorIndexSummary& expected,
                                                            std::string_view          primary,
                                                            const VectorIndexSlice&   slice);

    private:
        VectorSnapshot(const FsPath& path, const std::string& primary);
        DbConnector                           database_;
        VectorIndex                           index_;
        VectorIndexRoot                       root_;
        std::filesystem::path                 wal_path_;
        std::chrono::steady_clock::time_point deadline_;
    };
} // namespace Dfs
