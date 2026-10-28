#include "dfs/catalog_sync.h"

#include <sqlite3.h>

namespace {
    bool valid_cursor(const Dfs::FileLink &cursor) {
        return cursor.owner_id.is_zero() ? cursor.file_id.empty()
                                         : Dfs::Path::file_path(cursor.owner_id, cursor.file_id).has_value();
    }
    auto key(const Dfs::FileLink &cursor) {
        return std::pair { cursor.owner_id, cursor.file_id };
    }
} // namespace

bool Dfs::valid_catalog_request(const CatalogRowsRequest &request) {
    if (request.owners.size() > CatalogOwnerLimit || !valid_cursor(request.after))
        return false;
    std::set<ActorId> owners;
    for (const auto &owner : request.owners)
        if (owner.is_zero() || !owners.insert(owner).second)
            return false;
    return true;
}

bool Dfs::valid_catalog_page(const CatalogRowsPage &page, const CatalogRowsRequest &request) {
    if (!valid_catalog_request(request) || page.rows.size() > CatalogPageRows
        || (page.next.has_value() && (page.rows.empty() || !valid_cursor(page.next.value()))))
        return false;
    const std::set<ActorId> owners(request.owners.begin(), request.owners.end());
    auto                    previous = request.after;
    for (const auto &row : page.rows) {
        const FileLink current { .owner_id = row.owner_id, .file_id = row.file_id };
        if (!valid_cursor(current) || current.owner_id.is_zero() || key(current) <= key(previous)
            || (!owners.empty() && !owners.contains(row.owner_id)))
            return false;
        previous = current;
    }
    return !page.next.has_value() || page.next.value() == previous;
}

std::expected<Dfs::CatalogRowsPage, std::string> Dfs::read_catalog_page(
    const std::shared_ptr<DbConnector> &database,
    const CatalogRowsRequest           &request) {
    if (!valid_catalog_request(request))
        return std::unexpected("Invalid catalog page request");
    DbConnector reader(database->file());
    if (!reader.open(false))
        return std::unexpected("Catalog reader unavailable");
    std::string query = "SELECT * FROM ActorsFiles WHERE metadata_revision > 0 AND (owner_id,file_id) > (?,?)";
    if (!request.owners.empty()) {
        query += " AND owner_id IN (";
        for (std::size_t index = 0; index < request.owners.size(); ++index)
            query += index == 0 ? "?" : ",?";
        query += ')';
    }
    query += " ORDER BY owner_id,file_id LIMIT " + std::to_string(CatalogPageRows + 1);
    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(reader.getDb(), query.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        return std::unexpected("Cannot prepare catalog page");
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
    const auto                                                 bind = [&](int index, const std::string &value) {
        return sqlite3_bind_text(raw, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)
               == SQLITE_OK;
    };
    if (!bind(1, request.after.owner_id.to_string()) || !bind(2, request.after.file_id))
        return std::unexpected("Cannot bind catalog cursor");
    for (std::size_t index = 0; index < request.owners.size(); ++index)
        if (!bind(static_cast<int>(index + 3), request.owners[index].to_string()))
            return std::unexpected("Cannot bind catalog owner");
    CatalogRowsPage page;
    while (true) {
        const auto status = sqlite3_step(raw);
        if (status == SQLITE_DONE)
            return page;
        if (status != SQLITE_ROW)
            return std::unexpected("Cannot read catalog page");
        if (page.rows.size() == CatalogPageRows) {
            const auto &last = page.rows.back();
            page.next        = FileLink { .owner_id = last.owner_id, .file_id = last.file_id };
            return page;
        }
        DbRow       fields;
        std::size_t bytes = 0;
        for (int column = 0; column < sqlite3_column_count(raw); ++column) {
            if (sqlite3_column_type(raw, column) == SQLITE_NULL)
                continue;
            const auto *value = sqlite3_column_text(raw, column);
            const auto  size  = sqlite3_column_bytes(raw, column);
            if (value == nullptr || size < 0 || static_cast<std::size_t>(size) > 16 * 1024 - bytes)
                return std::unexpected("Catalog row exceeds the field budget");
            bytes += static_cast<std::size_t>(size);
            fields.emplace(sqlite3_column_name(raw, column),
                           std::string(reinterpret_cast<const char *>(value), size));
        }
        const auto row = Utils::from_dbrow<DirRow>(fields);
        if (!row.has_value())
            return std::unexpected("Stored catalog row is malformed");
        page.rows.push_back(row.value());
    }
}
