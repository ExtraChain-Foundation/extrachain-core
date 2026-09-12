#pragma once

#include "dfs/collection_template.h"
#include "utils/db_connector.h"

namespace Dfs {
    struct VectorDescriptor {
        unsigned           version = 1;
        CollectionTemplate schema;
        std::string        companion;
    };
    BOOST_DESCRIBE_STRUCT(VectorDescriptor, (), (version, schema, companion))

    inline constexpr std::size_t                   VectorDescriptorLimit = 128 * 1024;
    std::expected<CollectionTemplate, std::string> vector_storage_template(const CollectionTemplate& schema,
                                                                           bool                      encrypted);
    std::expected<std::optional<VectorDescriptor>, std::string> read_vector_descriptor(DbConnector& database);
    std::expected<void, std::string> store_vector_descriptor(DbConnector&            database,
                                                             const VectorDescriptor& descriptor,
                                                             bool                    encrypted);
    bool upsert_vector_row(DbConnector& database, const std::string& primary, const DbRow& row);
} // namespace Dfs
