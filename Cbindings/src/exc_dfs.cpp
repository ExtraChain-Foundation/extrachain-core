/*
 * ExtraChain Core — C FFI DFS Operations
 */

#include "exc_internal.h"

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "dfs/dfs_controller.h"
#include "utils/exc_utils.h"
#include "utils/fs_path.h"

using namespace exc_ffi;

/* ── Helper: map DfsError to ExcError ────────────────────────────── */

namespace {

ExcError map_dfs_error(Dfs::DfsError err) {
    switch (err) {
    case Dfs::DfsError::NotExists:    return EXC_ERR_DFS_NOT_EXISTS;
    case Dfs::DfsError::NotFile:      return EXC_ERR_DFS_NOT_FILE;
    case Dfs::DfsError::NotReadable:  return EXC_ERR_DFS_NOT_READABLE;
    case Dfs::DfsError::StorageFull:  return EXC_ERR_DFS_STORAGE_FULL;
    case Dfs::DfsError::AlreadyExists: return EXC_ERR_DFS_ALREADY_EXISTS;
    case Dfs::DfsError::DirError:     return EXC_ERR_DFS_DIR_ERROR;
    default:                          return EXC_ERR_DFS_UNKNOWN;
    }
}

ExcError map_export_error(ExportFileError err) {
    switch (err) {
    case ExportFileError::DirRowNotExists:    return EXC_ERR_DFS_EXPORT_DIR_ROW_NOT_EXISTS;
    case ExportFileError::FileNotReadyState:  return EXC_ERR_DFS_EXPORT_NOT_READY;
    case ExportFileError::IncorrectDfsPath:   return EXC_ERR_DFS_EXPORT_BAD_PATH;
    case ExportFileError::LocalFileNotExists: return EXC_ERR_DFS_EXPORT_LOCAL_MISSING;
    case ExportFileError::LocalFileNotValid:  return EXC_ERR_DFS_EXPORT_LOCAL_INVALID;
    case ExportFileError::OutupDirNotExits:   return EXC_ERR_DFS_EXPORT_OUTPUT_DIR_MISSING;
    case ExportFileError::NoWritePermissions: return EXC_ERR_DFS_EXPORT_NO_PERMISSIONS;
    case ExportFileError::OutputFileExists:   return EXC_ERR_DFS_EXPORT_OUTPUT_EXISTS;
    case ExportFileError::CopyError:          return EXC_ERR_DFS_EXPORT_COPY_ERROR;
    default:                                  return EXC_ERR_DFS_UNKNOWN;
    }
}

ExcError map_vector_error(DfsVectorError err) {
    switch (err) {
    case DfsVectorError::CollectionNotFound: return EXC_ERR_DFS_VECTOR_NOT_FOUND;
    case DfsVectorError::CollectionEmpty:    return EXC_ERR_DFS_VECTOR_EMPTY;
    case DfsVectorError::Adding:             return EXC_ERR_DFS_VECTOR_ADD;
    case DfsVectorError::Updating:           return EXC_ERR_DFS_VECTOR_UPDATE;
    case DfsVectorError::Deleting:           return EXC_ERR_DFS_VECTOR_DELETE;
    default:                                 return EXC_ERR_DFS_UNKNOWN;
    }
}

/* Helper: serialize a DbRow (unordered_map) to JSON */
std::string db_row_to_json(const DbRow& row) {
    return Json::serialize(row);
}

/* Helper: deserialize JSON to DbRow */
DbRow json_to_db_row(const std::string& json) {
    auto result = Json::deserialize<DbRow>(json);
    if (result.has_value()) return result.value();
    return DbRow {};
}

/* Helper: serialize vector of DbRows to JSON array */
std::string db_rows_to_json(const std::vector<DbRow>& rows) {
    return Json::serialize(rows);
}

} // anonymous namespace

extern "C" {

EXC_API ExcError exc_dfs_store_file(const char* owner_id, const char* file_path,
                                    const char* visual_folder, const char* visual_name,
                                    ExcDataSecurity security, char** out_dir_row_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_path);
    EXC_CHECK_NULL(visual_name);
    EXC_CHECK_NULL(out_dir_row_json);

    ExcError result = EXC_OK;
    *out_dir_row_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();
        auto* ac = gs.node->account_controller();

        if (ac->current_wallet().empty()) {
            result = EXC_ERR_NOT_LOGGED_IN;
            return;
        }

        ActorId oid{std::string(owner_id)};
        ActorId author = ac->current_wallet().id();
        std::string folder = visual_folder ? std::string(visual_folder) : "";

        auto res = dfs->store_file(oid, author,
                                   std::filesystem::path(std::string(file_path)),
                                   folder, std::string(visual_name),
                                   static_cast<Dfs::DataSecurity>(security));
        if (!res.has_value()) {
            result = map_dfs_error(res.error());
            return;
        }
        *out_dir_row_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_export_file(const char* owner_id, const char* file_id,
                                     const char* output_folder) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(output_folder);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        auto path = FsPath::create(std::string(output_folder));
        if (!path.has_value()) {
            result = EXC_ERR_DFS_EXPORT_BAD_PATH;
            return;
        }
        auto res = dfs->export_file(ActorId(std::string(owner_id)),
                                    std::string(file_id),
                                    path.value());
        if (!res.has_value()) {
            result = map_export_error(res.error());
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_store_vector(const char* owner_id, const char* visual_name,
                                      const char* template_owner_id,
                                      const char* template_file_id,
                                      ExcDataSecurity security, char** out_dir_row_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(visual_name);
    EXC_CHECK_NULL(template_owner_id);
    EXC_CHECK_NULL(template_file_id);
    EXC_CHECK_NULL(out_dir_row_json);

    ExcError result = EXC_OK;
    *out_dir_row_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();
        auto* ac = gs.node->account_controller();

        if (ac->current_wallet().empty()) {
            result = EXC_ERR_NOT_LOGGED_IN;
            return;
        }

        ActorId oid{std::string(owner_id)};
        ActorId author = ac->current_wallet().id();
        ActorId tmpl_owner{std::string(template_owner_id)};

        auto res = dfs->store_vector(oid, author, std::string(visual_name),
                                     tmpl_owner, std::string(template_file_id),
                                     static_cast<Dfs::DataSecurity>(security));
        if (!res.has_value()) {
            result = map_dfs_error(res.error());
            return;
        }
        *out_dir_row_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_vector_add_row(const char* owner_id, const char* file_id,
                                        const char* row_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(row_json);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        DbRow row = json_to_db_row(std::string(row_json));
        bool success = dfs->add_vector_row(ActorId(std::string(owner_id)),
                                           std::string(file_id), row);
        if (!success) {
            result = EXC_ERR_DFS_VECTOR_ADD;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_vector_update_row(const char* owner_id, const char* file_id,
                                           const char* row_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(row_json);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        DbRow row = json_to_db_row(std::string(row_json));
        bool success = dfs->update_vector_row(ActorId(std::string(owner_id)),
                                              std::string(file_id), row);
        if (!success) {
            result = EXC_ERR_DFS_VECTOR_UPDATE;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_vector_remove_row(const char* owner_id, const char* file_id,
                                           const char* primary_data) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(primary_data);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        bool success = dfs->remove_vector_row(ActorId(std::string(owner_id)),
                                              std::string(file_id),
                                              std::string(primary_data));
        if (!success) {
            result = EXC_ERR_DFS_VECTOR_DELETE;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_vector_read_rows(const char* owner_id, const char* file_id,
                                          const char* where_clause, char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        std::string where = where_clause ? std::string(where_clause) : "";

        auto res = dfs->read_vector_rows(ActorId(std::string(owner_id)),
                                         std::string(file_id), where);
        if (!res.has_value()) {
            result = map_vector_error(res.error());
            return;
        }
        *out_json = exc_strdup(db_rows_to_json(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_store_dictionary(const char* owner_id, const char* visual_name,
                                          ExcDataSecurity security,
                                          char** out_dir_row_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(visual_name);
    EXC_CHECK_NULL(out_dir_row_json);

    ExcError result = EXC_OK;
    *out_dir_row_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();
        auto* ac = gs.node->account_controller();

        if (ac->current_wallet().empty()) {
            result = EXC_ERR_NOT_LOGGED_IN;
            return;
        }

        ActorId oid{std::string(owner_id)};
        ActorId author = ac->current_wallet().id();

        auto res = dfs->store_dictionary(oid, author, std::string(visual_name),
                                         static_cast<Dfs::DataSecurity>(security));
        if (!res.has_value()) {
            result = map_dfs_error(res.error());
            return;
        }
        *out_dir_row_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_dictionary_set(const char* owner_id, const char* file_id,
                                        const char* key, const char* value) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(key);
    EXC_CHECK_NULL(value);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();
        auto* ac = gs.node->account_controller();

        if (ac->current_wallet().empty()) {
            result = EXC_ERR_NOT_LOGGED_IN;
            return;
        }

        ActorId oid{std::string(owner_id)};
        ActorId author = ac->current_wallet().id();

        bool success = dfs->dictionary_set_value(oid, std::string(file_id),
                                                 std::string(key), std::string(value),
                                                 author);
        if (!success) {
            result = EXC_ERR_DFS_UNKNOWN;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_dictionary_get(const char* owner_id, const char* file_id,
                                        const char* key, char** out_value) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(key);
    EXC_CHECK_NULL(out_value);

    ExcError result = EXC_OK;
    *out_value = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        auto val = dfs->read_dictionary(ActorId(std::string(owner_id)),
                                        std::string(file_id),
                                        std::string(key));
        if (!val.has_value()) {
            result = EXC_ERR_DFS_NOT_EXISTS;
            return;
        }
        *out_value = exc_strdup(val.value());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_dictionary_remove(const char* owner_id, const char* file_id,
                                           const char* key) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(key);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();
        auto* ac = gs.node->account_controller();

        if (ac->current_wallet().empty()) {
            result = EXC_ERR_NOT_LOGGED_IN;
            return;
        }

        ActorId oid{std::string(owner_id)};
        ActorId author = ac->current_wallet().id();

        bool success = dfs->dictionary_remove_value(oid, std::string(file_id),
                                                    std::string(key), author);
        if (!success) {
            result = EXC_ERR_DFS_UNKNOWN;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_list_files(const char* owner_id, char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();
        auto db = dfs->get_db_instance();

        auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(
            db, ActorId(std::string(owner_id)), 0,
            "AND state = 2 ORDER BY created DESC");

        if (!rows.has_value()) {
            result = EXC_ERR_DFS_NOT_EXISTS;
            return;
        }

        auto escape_json = [](const std::string& s) {
            std::string out;
            out.reserve(s.size());
            for (char c : s) {
                switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += c;
                }
            }
            return out;
        };

        std::string json = "[";
        bool first = true;
        for (const auto& row : rows.value()) {
            if (row.has_system_folder()) continue;

            if (!first) json += ",";
            first = false;

            json += "{";
            json += "\"file_id\":\"" + row.file_id + "\",";
            json += "\"name\":\"" + escape_json(row.name) + "\",";
            json += "\"size\":" + std::to_string(row.size) + ",";
            json += "\"type\":" + std::to_string(static_cast<int>(row.type)) + ",";
            json += "\"encrypted\":" + std::string(row.encryption ? "true" : "false") + ",";
            json += "\"folder\":\"" + escape_json(row.folder.value_or("")) + "\",";
            json += "\"created\":" + std::to_string(row.created);
            json += "}";
        }
        json += "]";

        *out_json = exc_strdup(json);
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_remove_file(const char* owner_id, const char* file_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        auto res = dfs->remove_stored_file(ActorId(std::string(owner_id)),
                                           std::string(file_id));
        if (!res.has_value()) {
            result = EXC_ERR_DFS_NOT_EXISTS;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_read_dir_row(const char* owner_id, const char* file_id,
                                      char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dfs = gs.node->dfs();

        auto res = dfs->read_file_status(ActorId(std::string(owner_id)),
                                         std::string(file_id));
        if (!res.has_value()) {
            result = map_dfs_error(res.error());
            return;
        }
        *out_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_request_file(const char* owner_id, const char* file_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        gs.node->dfs()->request_file(ActorId(std::string(owner_id)),
                                     std::string(file_id));
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_size_taken(uint64_t* out_bytes) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_bytes);

    bool ok = dispatch_sync([&]() {
        *out_bytes = GlobalState::instance().node->dfs()->sizeTaken();
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_size_limit(uint64_t* out_bytes) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_bytes);

    bool ok = dispatch_sync([&]() {
        *out_bytes = GlobalState::instance().node->dfs()->totalDfsSize();
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dfs_size_available(uint64_t* out_bytes) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_bytes);

    bool ok = dispatch_sync([&]() {
        *out_bytes = GlobalState::instance().node->dfs()->bytesAvailable();
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
