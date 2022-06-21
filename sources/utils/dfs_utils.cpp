#include "utils/dfs_utils.h"

#include "datastorage/actor.h"

std::vector<DBRow> DFS::Tables::ActorDirFile::getFileDataByHash(DBConnector *db, std::string hash) {
    std::string query = fmt::format("SELECT * FROM {} WHERE fileHash = '{}'", TableName, hash);
    return db->select(query);
}

std::vector<DBRow> DFS::Tables::ActorDirFile::getFileDataByName(DBConnector *db, std::string name) {
    std::string query = fmt::format("SELECT * FROM {} WHERE fileName = '{}'", TableName, name);
    return db->select(query);
}

std::string DFS::Tables::ActorDirFile::getLastName(DBConnector &db) {
    if (!db.isOpen()) {
        qFatal("DB not opened");
    }
    auto result = db.select(DFS::Tables::filesTableLast);
    auto prevRowOpt = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("fileName") : "";
    return lastFileName;
}

DBConnector DFS::Tables::ActorDirFile::actorDbConnector(const std::string &actorId) {
    DBConnector db(actorDbPath(actorId).string());
    db.open();
    return db;
}

std::filesystem::path DFS::Tables::ActorDirFile::actorDbPath(const std::string &actorId) {
    std::string path = DFSB::fsActrRoot + Utils::platformDelimeter() + actorId + Utils::platformDelimeter()
        + DFSB::fsMapName;
    return path;
}

std::vector<DFSP::DirRow> DFS::Tables::ActorDirFile::getDirRows(const std::string &actorId,
                                                                uint64_t lastModified) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        return {};
    }
    std::vector<DFSP::DirRow> dirRows;
    auto actrDirData =
        db.select(fmt::format("SELECT * FROM {} WHERE lastModified > {}", TableName, lastModified));
    for (auto &row : actrDirData) {
        DFSP::DirRow dirRow = { .fileHash = row["fileHash"],
                                .fileHashPrev = row["fileHashPrev"],
                                .filePath = row["filePath"],
                                .fileName = row["fileName"],
                                .fileSize = std::stoull(row["fileSize"]),
                                .lastModified = std::stoull(row["lastModified"]) };
        dirRows.push_back(dirRow);
    }

    return dirRows;
}

DFSP::DirRow DFS::Tables::ActorDirFile::getDirRow(const std::string &actorId, const std::string &fileName) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        qFatal("DB Error");
        return {};
    }

    auto rows = db.select(fmt::format("SELECT * FROM {} WHERE fileName = '{}';", TableName, fileName));
    if (rows.empty()) {
        return {};
    }

    auto &row = rows[0];
    DFSP::DirRow dirRow = { .fileHash = row["fileHash"],
                            .fileHashPrev = row["fileHashPrev"],
                            .filePath = row["filePath"],
                            .fileName = row["fileName"],
                            .fileSize = std::stoull(row["fileSize"]),
                            .lastModified = std::stoull(row["lastModified"]) };

    return dirRow;
}

bool DFS::Tables::ActorDirFile::addDirRows(const std::string &actorId,
                                           const std::vector<Packets::DirRow> &dirRows) {
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + actorId + pathDelim + DFSB::fsMapName;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
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

    if (p.find(DFSB::separator) == std::wstring::npos) {
        boost::replace_all(p, L"/", DFSB::separator);
        boost::replace_all(p, L"\\", DFSB::separator);
    }

    return p;
}

std::filesystem::path DFS::Path::filePath(const ActorId &actorId, const std::string &fileName) {
    return DFSB::fsActrRoot + Utils::platformDelimeter() + actorId.toStdString() + Utils::platformDelimeter()
        + fileName;
}
