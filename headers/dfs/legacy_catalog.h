#pragma once

#include "dfs/dfs_utils.h"

namespace Dfs {
    // Outgoing update metadata only. Current catalog admission never uses this format.
    struct LegacyFileRow {
        ActorId                    actor_id;
        ActorId                    owner_id;
        std::string                file_id;
        std::optional<std::string> prev_file_id;
        std::string                hash;
        std::optional<std::string> folder;
        std::string                name;
        std::size_t                size          = 0;
        std::uint64_t              created       = 0;
        std::uint64_t              last_modified = 0;
        FileType                   type          = FileType::File;
        bool                       encryption    = false;
        FileState                  state         = FileState::Known;
        Signature                  sign { };
    };
    BOOST_DESCRIBE_STRUCT(LegacyFileRow,
                          (),
                          (actor_id,
                           owner_id,
                           file_id,
                           prev_file_id,
                           hash,
                           folder,
                           name,
                           size,
                           created,
                           last_modified,
                           type,
                           encryption,
                           state,
                           sign))

    inline std::optional<LegacyFileRow> legacy_public_file(const DirRow &row, const Actor<KeyPrivate> &owner) {
        if (row.owner_id != owner.id() || row.actor_id != owner.id() || row.type != FileType::File
            || row.encryption || row.state != FileState::Ready || row.metadata_revision == 0)
            return std::nullopt;
        const auto verified = owner.key().verify(row.calculate_hash(owner.id()), row.sign);
        if (!verified.has_value() || !verified.value())
            return std::nullopt;
        const auto signature = owner.key().sign(row.calculate_legacy_hash(owner.id()));
        if (!signature.has_value())
            return std::nullopt;
        return LegacyFileRow { row.actor_id, row.owner_id,   row.file_id, row.prev_file_id, row.hash,
                               row.folder,   row.name,       row.size,    row.created,      row.last_modified,
                               row.type,     row.encryption, row.state,   signature.value() };
    }
} // namespace Dfs
