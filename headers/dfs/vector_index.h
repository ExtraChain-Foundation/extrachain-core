#pragma once

#include <expected>
#include <string>
#include <vector>

#include "utils/db_connector.h"

namespace Dfs {
    struct VectorIndexSummary {
        std::string   prefix;
        std::string   hash;
        std::uint64_t rows                                        = 0;
        std::uint64_t bytes                                       = 0;
        bool          operator==(const VectorIndexSummary&) const = default;
    };
    BOOST_DESCRIBE_STRUCT(VectorIndexSummary, (), (prefix, hash, rows, bytes))

    struct VectorIndexRoot {
        unsigned           version = 1;
        std::string        schema;
        std::string        hash;
        VectorIndexSummary tree;
        std::string        legacy_hash;
    };
    BOOST_DESCRIBE_STRUCT(VectorIndexRoot, (), (version, schema, hash, tree))

    class VectorIndex {
    public:
        explicit VectorIndex(DbConnector& database, std::string primary);
        std::expected<VectorIndexRoot, std::string> root();
        // The caller's transaction must include the Vector write and this update.
        std::expected<void, std::string>                            update(std::string_view primary);
        std::expected<std::vector<VectorIndexSummary>, std::string> children(std::string_view prefix);
        std::expected<VectorIndexSummary, std::string>              subtree(std::string_view prefix);
        std::expected<std::vector<DbRow>, std::string>              page(std::string_view prefix,
                                                                         std::string_view after,
                                                                         std::size_t      limit);
        static std::string                                          key_hash(std::string_view primary);
        static std::string                                          row_hash(const DbRow& row);
        static bool                                                 valid_root(const VectorIndexRoot& root);
        static std::expected<VectorIndexSummary, std::string>       summarize_children(
            std::string_view                       prefix,
            const std::vector<VectorIndexSummary>& children);
        static std::expected<VectorIndexSummary, std::string> summarize_rows(std::string_view          primary,
                                                                             const std::vector<DbRow>& rows);

    private:
        void               prepare();
        void               rebuild(const std::string& legacy_hash);
        VectorIndexRoot    read_root();
        void               update_row(const DbRow& row);
        VectorIndexSummary insert(const VectorIndexSummary& current, const DbRow& row);
        std::string        schema_hash();

        DbConnector& database_;
        std::string  primary_;
        std::string  quoted_primary_;
    };
} // namespace Dfs
