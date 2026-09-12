#include "dfs/catalog_metadata.h"

#include <limits>

namespace {
    bool digest(std::string_view value) {
        return value.size() == 64 && std::ranges::all_of(value, [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    }

    bool mutable_vector(const Dfs::DirRow &row) {
        return row.type == Dfs::FileType::Vector || row.type == Dfs::FileType::Dictionary;
    }
} // namespace

bool Dfs::valid_catalog_metadata(const DirRow &row, const Actor<KeyPublic> &signer) {
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (row.owner_id.is_zero() || !digest(row.file_id) || row.metadata_revision == 0
        || row.metadata_revision > maximum || row.sign.size() != crypto_sign_BYTES) {
        return false;
    }
    if (row.state == FileState::Removed) {
        if (signer.id() != row.owner_id) {
            return false;
        }
    } else {
        const bool invite = row.type == FileType::File && row.encryption && row.folder == ":DApp:Chat:Invite"
                            && row.size <= 1024 * 1024;
        if (row.actor_id.is_zero() || (signer.id() != row.owner_id && !(invite && signer.id() == row.actor_id))
            || row.name.empty() || row.name.size() > 4096
            || (row.folder.has_value() && row.folder.value().size() > 4096)
            || (row.prev_file_id.has_value() && !row.prev_file_id.value().empty()
                && !digest(row.prev_file_id.value()))
            || (row.type != FileType::File && row.type != FileType::Folder && row.type != FileType::Collection
                && row.type != FileType::Vector && row.type != FileType::Dictionary)
            || row.size > maximum || row.created > maximum || row.last_modified > maximum
            || (row.type != FileType::Folder && !digest(row.hash))
            || (mutable_vector(row) ? !digest(row.template_hash) : !row.template_hash.empty())
            || (row.state != FileState::Known && row.state != FileState::Ready && row.state != FileState::Partial
                && row.state != FileState::Processing)) {
            return false;
        }
    }
    const auto verified = signer.key().verify(row.calculate_hash(row.owner_id), row.sign);
    return verified.has_value() && verified.value();
}

Dfs::DirRow Dfs::catalog_tombstone(const ActorId     &owner,
                                   const std::string &file_id,
                                   std::uint64_t      revision,
                                   const Signature   &signature) {
    DirRow row { };
    row.owner_id          = owner;
    row.actor_id          = owner;
    row.file_id           = file_id;
    row.metadata_revision = revision;
    row.last_modified     = revision;
    row.state             = FileState::Removed;
    row.sign              = signature;
    return row;
}

std::expected<Dfs::CatalogUpdate, std::string> Dfs::store_catalog_metadata(
    const std::shared_ptr<DbConnector> &database,
    const DirRow                       &received,
    const Actor<KeyPublic>             &signer,
    bool                                local_payload) {
    if (!valid_catalog_metadata(received, signer)) {
        return std::unexpected("Invalid catalog signature or fields");
    }
    auto row =
        received.state == FileState::Removed
            ? catalog_tombstone(received.owner_id, received.file_id, received.metadata_revision, received.sign)
            : received;
    // A dedicated SQLite connection keeps this transaction separate from other
    // storage workers that use the shared catalog connection.
    auto transaction = std::make_shared<DbConnector>(database->file());
    if (!transaction->open(false) || !transaction->query("BEGIN IMMEDIATE")) {
        return std::unexpected("Catalog transaction unavailable");
    }
    struct Rollback {
        DbConnector &database;
        bool         active = true;
        ~Rollback() {
            if (active)
                database.query("ROLLBACK");
        }
    } rollback { *transaction };
    const auto    found = Tables::DirsFile::ActorSpace::get_dir_row(transaction, row.owner_id, row.file_id);
    CatalogUpdate result { found.has_value() ? std::optional(found.value()) : std::nullopt, row, false };
    if (found.has_value()) {
        const auto &previous = found.value();
        // Deletion is terminal for this file ID. Replaying its valid signature
        // cannot resurrect the entry or repeat a state change.
        if (previous.state == FileState::Removed && previous.metadata_revision != 0
            && (row.state != FileState::Removed || row.metadata_revision <= previous.metadata_revision)) {
            result.current = previous;
            return result;
        }
        if (row.state != FileState::Removed && previous.metadata_revision != 0) {
            if (row.actor_id != previous.actor_id || row.type != previous.type
                || row.encryption != previous.encryption || row.template_hash != previous.template_hash
                || row.created != previous.created) {
                return std::unexpected("Immutable catalog identity differs");
            }
            if (row.metadata_revision < previous.metadata_revision
                || (row.metadata_revision == previous.metadata_revision
                    && row.calculate_hash(row.owner_id) <= previous.calculate_hash(previous.owner_id))) {
                result.current = previous;
                return result;
            }
        }
        if (row.state != FileState::Removed && mutable_vector(row) && previous.state == FileState::Ready) {
            row.hash          = previous.hash;
            row.size          = previous.size;
            row.last_modified = previous.last_modified;
            row.state         = FileState::Ready;
        } else if (row.state != FileState::Removed) {
            row.state =
                (local_payload || row.type == FileType::Folder
                 || (previous.state == FileState::Ready && row.hash == previous.hash && row.size == previous.size))
                    ? FileState::Ready
                    : FileState::Known;
        }
    } else if (row.state != FileState::Removed) {
        row.state = local_payload || row.type == FileType::Folder ? FileState::Ready : FileState::Known;
    }
    auto fields = Utils::to_dbrow(row);
    if (!row.prev_file_id.has_value() || row.prev_file_id.value().empty()) {
        fields.erase("prev_file_id");
    }
    std::string columns, values, updates;
    for (const auto &[name, value] : fields) {
        if (!columns.empty()) {
            columns += ',';
            values += ',';
        }
        columns += '"' + name + '"';
        values += ':' + name;
        if (name != "owner_id" && name != "file_id") {
            if (!updates.empty()) {
                updates += ',';
            }
            updates += '"' + name + "\"=excluded.\"" + name + '"';
        }
    }
    if (!fields.contains("prev_file_id")) {
        updates += ",prev_file_id=excluded.prev_file_id";
    }
    if (!transaction->query("INSERT INTO ActorsFiles(" + columns + ") VALUES(" + values
                                + ") ON CONFLICT(owner_id,file_id) DO UPDATE SET " + updates,
                            Tables::DirsFile::TableNameActorsFiles,
                            fields)
        || !transaction->query("COMMIT")) {
        return std::unexpected("Catalog update failed");
    }
    rollback.active = false;
    result.current  = std::move(row);
    result.changed  = true;
    return result;
}
