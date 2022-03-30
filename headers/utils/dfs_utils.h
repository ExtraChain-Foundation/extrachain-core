#ifndef DFS_UTILS_H
#define DFS_UTILS_H

#include <string>
#include <vector>

#include "msgpack.hpp"
#include "utils/db_connector.h"
#include <boost/algorithm/string/replace.hpp>

namespace Utils {
static const std::string getPlatformDelimeterDFS() {
    char del;
    std::wcstombs(&del, &std::filesystem::path::preferred_separator, 1);
    std::string pathDelim;
    pathDelim.push_back(del);
    return pathDelim;
}
}

namespace DFS {
namespace Basic {
    static const std::string fsActrRoot = "dfs";
    static const std::string fsMapName = ".dir";
    static const std::string serviceStoragePath = "service";
    static const std::string serviceDfsPath =
        serviceStoragePath + Utils::getPlatformDelimeterDFS() + DFS::Basic::fsActrRoot;
    static const long long sectionSize = 256;
    static const int encSectionSize = 256;
}
namespace Packets {
    struct AddFileMessage {
        std::string Actor;
        std::string FileHash;
        std::string Path;
        unsigned long long Size;
        MSGPACK_DEFINE(Actor, FileHash, Path, Size);
    };

    struct RequestFileSegmentMessage {
        std::string Actor;
        std::string FileHash;
        std::string Path;
        long long Offset;
        MSGPACK_DEFINE(Actor, FileHash, Path, Offset);
    };

    struct RemoveFileMessage {
        std::string Actor;
        std::string FileHash;
        MSGPACK_DEFINE(Actor, FileHash);
    };

    struct EditSegmentMessage {
        std::string Actor;
        std::string FileHash;
        std::string Data;
        long long Offset;
        MSGPACK_DEFINE(Actor, FileHash, Data, Offset);
    };

    struct AddSegmentMessage {
        std::string Actor;
        std::string FileHash;
        std::string Data;
        long long Offset;
        MSGPACK_DEFINE(Actor, FileHash, Data, Offset);
    };

    struct DeleteSegmentMessage {
        std::string Actor;
        std::string FileHash;
        long long Offset;
        long long Size;
        MSGPACK_DEFINE(Actor, FileHash, Offset, Size);
    };
}
namespace Tables {
    namespace ActorDirFile {
        static const std::string TableName = "FilesTable";
        static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
            + " ("
              "fileHash     TEXT PRIMARY KEY NOT NULL, "
              "fileHashPrev TEXT             NOT NULL, "
              "filePath     TEXT             NOT NULL, "
              "fileSize     TEXT             NOT NULL"
              ");";
        std::vector<DBRow> getFileDataByHash(DBConnector *db, std::string hash);
    }
    namespace LocalDirFile {
        static const std::string TableName = "FileSegmentsTable";
        static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
            + " ("
              "fileHash         TEXT PRIMARY KEY NOT NULL, "
              "fileHashPrev     TEXT             NOT NULL, "
              "filePath         TEXT             NOT NULL, "
              "fileSegmentBegin TEXT             NOT NULL, "
              "fileSegmentEnd   TEXT             NOT NULL, "
              "fileSize         TEXT             NOT NULL"
              ");";
        std::vector<DBRow> getFileDataByHash(DBConnector *db, std::string hash);
    }

    static const std::string permissionTable = "PermissionTable";
    static const std::string permissionTableCreate = "CREATE TABLE IF NOT EXISTS " + permissionTable
        + " ("
          "fileHash   TEXT NOT NULL, "
          "permission TEXT NOT NULL, "
          "userId     TEXT NOT NULL,"
          "signature  TEXT NOT NULL"
          ");";

    static const std::string filesTableLast =
        "SELECT * FROM " + DFS::Tables::ActorDirFile::TableName + " ORDER BY fileHash DESC LIMIT 1";
    static const std::string filesTableFull = "SELECT * FROM " + DFS::Tables::ActorDirFile::TableName;
}
namespace Path {
    std::string convertPathToPlatform(std::string path);
}
enum Encryption
{
    Public = 0,
    Encrypted = 1
};
}
#endif // DFS_UTILS_H
