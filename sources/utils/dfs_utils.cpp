#include "utils/dfs_utils.h"

std::vector<DBRow> DFS::Tables::ActorDirFile::getFileDataByHash(DBConnector *db, std::string hash) {
    std::string query = "SELECT * FROM " + TableName + " WHERE fileHash = '" + hash + "' "
        /*+ "OR fileHashPrev = '" + hash + "' "*/;
    return db->select(query);
}

std::string DFS::Tables::ActorDirFile::getLastHash(DBConnector &db) {
    if (!db.isOpen()) {
        qFatal("DB not opened");
    }
    auto result = db.select(DFS::Tables::filesTableLast);
    auto prevRowOpt = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileHash = prevRowOpt ? prevRowOpt->at("fileHash") : "";
    return lastFileHash;
}

DBConnector DFS::Tables::ActorDirFile::actorDbConnector(const std::string &actorId) {
    DBConnector db;
    db.open(actorDbPath(actorId).string());
    return db;
}

std::filesystem::path DFS::Tables::ActorDirFile::actorDbPath(const std::string &actorId) {
    std::string path = DFS::Basic::fsActrRoot + Utils::platformDelimeter() + actorId
        + Utils::platformDelimeter() + DFS::Basic::fsMapName;
    return path;
}

std::vector<DFS::Packets::DirRow> DFS::Tables::ActorDirFile::getDirRows(const std::string &actorId,
                                                                        uint64_t lastModified) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        return {};
    }
    std::vector<DFS::Packets::DirRow> dirRows;
    auto actrDirData = db.select("SELECT * FROM " + DFS::Tables::ActorDirFile::TableName
                                 + " WHERE lastModified > " + std::to_string(lastModified));
    for (auto &row : actrDirData) {
        DFS::Packets::DirRow dirRow = { .fileHash = row["fileHash"],
                                        .fileHashPrev = row["fileHashPrev"],
                                        .filePath = row["filePath"],
                                        .fileSize = std::stoull(row["fileSize"]),
                                        .lastModified = std::stoull(row["lastModified"]) };
        dirRows.push_back(dirRow);
    }

    return dirRows;
}

DFS::Packets::DirRow DFS::Tables::ActorDirFile::getDirRow(const std::string &actorId,
                                                          const std::string &fileHash) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        qFatal("DB Error");
        return {};
    }

    auto rows = db.select("SELECT * FROM " + DFS::Tables::ActorDirFile::TableName + " WHERE fileHash = '"
                          + fileHash + "';");
    if (rows.empty()) {
        return {};
    }

    auto &row = rows[0];
    DFS::Packets::DirRow dirRow = { .fileHash = row["fileHash"],
                                    .fileHashPrev = row["fileHashPrev"],
                                    .filePath = row["filePath"],
                                    .fileSize = std::stoull(row["fileSize"]),
                                    .lastModified = std::stoull(row["lastModified"]) };

    return dirRow;
}

bool DFS::Tables::ActorDirFile::addDirRows(const std::string &actorId,
                                           const std::vector<Packets::DirRow> &dirRows) {
    std::string pathDelim = Utils::platformDelimeter();
    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + actorId + pathDelim + DFS::Basic::fsMapName;
    if (!actrDirFile.open(actrDirFilePath)) {
        return false;
    }

    for (auto &dirRow : dirRows) {
        if (dirRow.fileHash.empty()) {
            qFatal("Oh no why");
        }
        auto row = DBRow { { "fileHash", dirRow.fileHash },
                           { "fileHashPrev", dirRow.fileHashPrev },
                           { "filePath", dirRow.filePath },
                           { "fileSize", std::to_string(dirRow.fileSize) },
                           { "lastModified", std::to_string(dirRow.lastModified) } };
        actrDirFile.insert(DFS::Tables::ActorDirFile::TableName, row);
    }

    return true;
}

std::filesystem::path DFS::Path::convertPathToPlatform(const std::filesystem::path &path) {
    std::wstring p = path.wstring();

    if (p.find(DFS::Basic::separator) == std::wstring::npos) {
        boost::replace_all(p, L"/", DFS::Basic::separator);
        boost::replace_all(p, L"\\", DFS::Basic::separator);
    }

    return p;
}

std::filesystem::path DFS::Path::filePath(const std::string &actorId, const std::string &fileHash) {
    return DFS::Basic::fsActrRoot + Utils::platformDelimeter() + actorId + Utils::platformDelimeter()
        + fileHash;
}
