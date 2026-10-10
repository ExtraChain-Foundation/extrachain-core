#pragma once

#include "dfs/dfs_utils.h"

namespace Dfs {
    inline constexpr std::size_t CatalogPageRows   = 128;
    inline constexpr std::size_t CatalogOwnerLimit = 4096;
    inline constexpr std::size_t CatalogPageBytes  = 4 * 1024 * 1024;

    struct CatalogRowsRequest {
        std::vector<ActorId> owners;
        FileLink             after;
    };
    BOOST_DESCRIBE_STRUCT(CatalogRowsRequest, (), (owners, after))

    struct CatalogRowsPage {
        std::vector<DirRow>     rows;
        std::optional<FileLink> next;
    };
    BOOST_DESCRIBE_STRUCT(CatalogRowsPage, (), (rows, next))

    bool valid_catalog_request(const CatalogRowsRequest &request);
    bool valid_catalog_page(const CatalogRowsPage &page, const CatalogRowsRequest &request);
    std::expected<CatalogRowsPage, std::string> read_catalog_page(const std::shared_ptr<DbConnector> &database,
                                                                  const CatalogRowsRequest           &request);
} // namespace Dfs
