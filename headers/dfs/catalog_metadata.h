#pragma once

#include "chain/actor.h"
#include "dfs/dfs_utils.h"

namespace Dfs {
    struct CatalogUpdate {
        std::optional<DirRow> previous;
        DirRow                current;
        bool                  changed = false;
    };

    bool   valid_catalog_metadata(const DirRow &row, const Actor<KeyPublic> &signer);
    DirRow catalog_tombstone(const ActorId     &owner,
                             const std::string &file_id,
                             std::uint64_t      revision,
                             const Signature   &signature);
    std::expected<CatalogUpdate, std::string> store_catalog_metadata(const std::shared_ptr<DbConnector> &database,
                                                                     const DirRow                       &row,
                                                                     const Actor<KeyPublic>             &signer,
                                                                     bool local_payload = false);
} // namespace Dfs
