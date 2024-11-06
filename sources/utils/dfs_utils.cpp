#include "utils/dfs_utils.h"

#include "datastorage/actor.h"

std::vector<DBRow> Dfs::Tables::ActorDirFile::getFileDataByHash(DBConnector *db, std::string hash) {
    std::string query = fmt::format("SELECT * FROM {} WHERE hash = '{}'", TableName, hash);
    return db->select(query);
}

std::vector<DBRow> Dfs::Tables::ActorDirFile::getFileDataByName(DBConnector *db, std::string name) {
    std::string query = fmt::format("SELECT * FROM {} WHERE fileId = '{}'", TableName, name);
    return db->select(query);
}

std::string Dfs::Tables::ActorDirFile::getLastFileId(DBConnector &db) {
    if (!db.isOpen()) {
        qFatal("DB not opened");
    }
    auto        result       = db.select(Dfs::Tables::filesTableLast);
    auto        prevRowOpt   = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("fileId") : "";
    return lastFileName;
}

DBConnector Dfs::Tables::ActorDirFile::actorDbConnector(const ActorId &actorId) {
    DBConnector db(actorDbPath(actorId).string());
    db.open();
    return db;
}

std::filesystem::path Dfs::Tables::ActorDirFile::actorDbPath(const ActorId &actorId) {
    std::string path = DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.toString()
                       + Utils::platformDelimeter() + DfsB::fsMapName;
    return path;
}

std::filesystem::path
Dfs::Tables::ActorDirFile::storjDbPath(const ActorId &actorId, const std::string &storjName) {
    std::string path = DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.toString()
                       + Utils::platformDelimeter() + storjName;
    return path;
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError>
Dfs::Tables::ActorDirFile::getDirRows(const ActorId &actorId, std::uint64_t lastModified) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    std::vector<Dfs::DirRow> dirRows;
    auto                     actrDirData =
        db.select(fmt::format("SELECT * FROM {} WHERE lastModified >= {}", TableName, lastModified));

    for (auto &row : actrDirData) {
        auto dirRow = Utils::fromDbRow<Dfs::DirRow>(row);
        if (dirRow.has_value()) {
            dirRow->actorId = actorId;
            dirRows.push_back(dirRow.value());
        }
    }

    return dirRows;
}

std::expected<Dfs::DirRow, Dfs::DfsError>
Dfs::Tables::ActorDirFile::getDirRow(const ActorId &actorId, const std::string &fileId) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    auto rows = db.select(fmt::format("SELECT * FROM {} WHERE fileId = '{}';", TableName, fileId));
    if (rows.empty()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    auto &row    = rows[0];
    auto  dirRow = Utils::fromDbRow<Dfs::DirRow>(row);

    if (!dirRow.has_value()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    dirRow->actorId = actorId;
    return dirRow.value();
}

bool Dfs::Tables::ActorDirFile::addDirRow(const ActorId &actorId, DirRow &dirRow) {
    auto dirFile = actorDbConnector(actorId);

    if (!dirFile.isOpen()) {
        return false;
    }

    auto currentSecs = Utils::currentDateSecs();
    auto fileIdPrev  = DfsT::ActorDirFile::getLastFileId(dirFile);

    dirRow.created      = currentSecs;
    dirRow.lastModified = currentSecs;
    dirRow.fileIdPrev   = fileIdPrev;

    auto dirRowDb = Utils::toDbRow(dirRow);
    dirRowDb.erase("actorId");
    bool res = dirFile.insert(Dfs::Tables::ActorDirFile::TableName, dirRowDb);

    return res;
}

bool Dfs::Tables::ActorDirFile::addDirRows(const ActorId &actorId, const std::vector<DirRow> &dirRows) {
    auto dirFile = actorDbConnector(actorId);

    if (!dirFile.isOpen()) {
        return false;
    }

    for (auto &dirRow : dirRows) {
        if (dirRow.hash.empty()) {
            continue;
        }

        auto dirRowDb = Utils::toDbRow(dirRow);
        dirRowDb.erase("actorId");
        dirFile.insert(Dfs::Tables::ActorDirFile::TableName, dirRowDb);
    }

    return true;
}

std::filesystem::path Dfs::Path::convertPathToPlatform(const std::filesystem::path &path) {
    std::wstring p = path.wstring();

    if (p.substr(0, Utils::filePrefix.length()) == Utils::filePrefix) {
        p = p.substr(Utils::filePrefix.length());
    }

    if (p.find(DfsB::separator) == std::wstring::npos) {
        boost::replace_all(p, L"/", DfsB::separator);
        boost::replace_all(p, L"\\", DfsB::separator);
    }

    return p;
}

std::filesystem::path Dfs::Path::filePath(const ActorId &actorId, const std::string &fileName) {
    return DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.toString() + Utils::platformDelimeter()
           + fileName;
}

std::filesystem::path Dfs::Path::actorPath(const ActorId &actorId) {
    return DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.toString();
}

int Dfs::Tables::ActorDirFile::totalFileSize(const ActorId &actorId) {
    auto db = actorDbConnector(actorId);
    if (!db.isOpen()) {
        qFatal("DB Error");
        return 0;
    }

    auto count = db.count(TableName);
    if (count == 0)
        return 0;

    auto row = db.select(fmt::format("SELECT SUM(size) from {}", TableName)).at(0);
    return std::stoi(row["SUM(size)"]);
}

std::uint64_t
Dfs::Tables::ActorDirFile::dataAmountStoredSize(const ActorId &actorId, const std::string &storjName) {
    DBConnector db(storjDbPath(actorId, storjName).string());
    db.open();
    if (!db.isOpen()) {
        qFatal("DB Error");
        return 0;
    }

    auto count = db.select(fmt::format("SELECT COUNT(size) from {}", DfsF::TableNameFragments))[0];
    if (std::stoi(count["COUNT(size)"]) == 0) {
        return 0;
    }

    auto  rows = db.select(fmt::format("SELECT SUM(size) from {}", DfsF::TableNameFragments));
    auto &row  = rows[0];

    return std::stoull(row["SUM(size)"]);
}
