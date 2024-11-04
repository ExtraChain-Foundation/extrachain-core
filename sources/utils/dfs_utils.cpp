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
    auto        result       = db.select(DFS::Tables::filesTableLast);
    auto        prevRowOpt   = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("fileName") : "";
    return lastFileName;
}

DBConnector DFS::Tables::ActorDirFile::actorDbConnector(const ActorId &actorId) {
    DBConnector db(actorDbPath(actorId).string());
    db.open();
    return db;
}

std::filesystem::path DFS::Tables::ActorDirFile::actorDbPath(const ActorId &actorId) {
    std::string path = DFSB::fsActrRoot + Utils::platformDelimeter() + actorId.toStdString()
                       + Utils::platformDelimeter() + DFSB::fsMapName;
    return path;
}

std::filesystem::path
DFS::Tables::ActorDirFile::storjDbPath(const ActorId &actorId, const std::string &storjName) {
    std::string path = DFSB::fsActrRoot + Utils::platformDelimeter() + actorId.toStdString()
                       + Utils::platformDelimeter() + storjName;
    return path;
}

std::vector<DFS::DirRow>
DFS::Tables::ActorDirFile::getDirRows(const ActorId &actorId, std::uint64_t lastModified) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        return {};
    }
    std::vector<DFS::DirRow> dirRows;
    auto                     actrDirData =
        db.select(fmt::format("SELECT * FROM {} WHERE lastModified > {}", TableName, lastModified));
    for (auto &row : actrDirData) {
        auto dirRow = Utils::fromDbRow<DFS::DirRow>(row);
        if (dirRow.has_value()) {
            dirRows.push_back(dirRow.value());
        }
    }

    return dirRows;
}

// TODO: expected?
DFS::DirRow DFS::Tables::ActorDirFile::getDirRow(const ActorId &actorId, const std::string &fileId) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        qFatal("DB Error");
        return {};
    }

    auto rows = db.select(fmt::format("SELECT * FROM {} WHERE fileId = '{}';", TableName, fileId));
    if (rows.empty()) {
        return {};
    }

    auto &row    = rows[0];
    auto  dirRow = Utils::fromDbRow<DFS::DirRow>(row);

    return dirRow.value();
}

bool DFS::Tables::ActorDirFile::addDirRows(const ActorId &actorId, const std::vector<DirRow> &dirRows) {
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath =
        DFSB::fsActrRoot + pathDelim + actorId.toStdString() + pathDelim + DFSB::fsMapName;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        return false;
    }

    for (auto &dirRow : dirRows) {
        if (dirRow.hash.empty()) {
            // Uncommited row below and find out why hash is empty.
            // qFatal("Oh no why. Filehash is empty.");
        }

        auto row = Utils::toDbRow(dirRow);
        actrDirFile.insert(DFS::Tables::ActorDirFile::TableName, row);
    }

    return true;
}

std::filesystem::path DFS::Path::convertPathToPlatform(const std::filesystem::path &path) {
    std::wstring p = path.wstring();

    if (p.substr(0, Utils::filePrefix.length()) == Utils::filePrefix) {
        p = p.substr(Utils::filePrefix.length());
    }

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

std::filesystem::path DFS::Path::actorPath(const ActorId &actorId) {
    return DFSB::fsActrRoot + Utils::platformDelimeter() + actorId.toStdString();
}

int DFS::Tables::ActorDirFile::totalFileSize(const ActorId &actorId) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        qFatal("DB Error");
        return 0;
    }

    auto count = db.select(fmt::format("SELECT COUNT(fileSize) from {}", TableName))[0];
    if (std::stoi(count["COUNT(fileSize)"]) == 0) {
        return 0;
    }

    auto row = db.select(fmt::format("SELECT SUM(fileSize) from {}", TableName)).at(0);

    return std::stoi(row["SUM(fileSize)"]);
}

std::uint64_t
DFS::Tables::ActorDirFile::dataAmountStoredSize(const ActorId &actorId, const std::string &storjName) {
    DBConnector db(storjDbPath(actorId, storjName).string());
    db.open();
    if (!db.isOpen()) {
        qFatal("DB Error");
        return 0;
    }

    auto count = db.select(fmt::format("SELECT COUNT(size) from {}", DFSF::TableNameFragments))[0];
    if (std::stoi(count["COUNT(size)"]) == 0) {
        return 0;
    }

    auto  rows = db.select(fmt::format("SELECT SUM(size) from {}", DFSF::TableNameFragments));
    auto &row  = rows[0];

    return std::stoull(row["SUM(size)"]);
}
