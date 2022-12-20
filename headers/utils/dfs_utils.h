#ifndef DFS_UTILS_H
#define DFS_UTILS_H

#include <filesystem>
#include <string>
#include <vector>

#include "utils/bignumber.h"
#include "utils/db_connector.h"
#include <boost/algorithm/string/replace.hpp>
#include <msgpack.hpp>

class ActorId;

namespace Tools {
template <typename T>
std::vector<unsigned char> typeToByteArray(T integerValue) {
    std::vector<unsigned char> res;
    unsigned char *b = (unsigned char *)(&integerValue);
    unsigned char *e = b + sizeof(T);
    std::copy(b, e, back_inserter(res));
    return res;
}

template <typename T>
T byteArrayToType(std::vector<unsigned char> value) {
    T *res;
    res = reinterpret_cast<T *>(value.data());
    return *res;
}

template <typename T>
std::string typeToStdStringBytes(T integerValue) {
    std::string res;
    unsigned char *b = (unsigned char *)(&integerValue);
    unsigned char *e = b + sizeof(T);
    std::copy(b, e, back_inserter(res));
    return res;
}

template <typename T>
T stdStringBytesToType(std::string value) {
    T *res;
    res = reinterpret_cast<T *>(value.data());
    return *res;
}
}
namespace Utils {
std::string platformDelimeter();

inline static uint64_t globalVariableOfDfsSize = 0;
}

namespace DFS {
namespace Basic {
    static const std::string fsActrRoot = "dfs";
    static const std::wstring fsActrRootW = L"dfs";
    static const std::string fsMapName = ".dir";
    static const std::string dirsPath = "dfs/.dirs";
    static const uint64_t sectionSize = /*2097152*/ 524228;
    static const uint64_t maxSectionSize = 209715200;
    static const uint64_t historicalChainSectionSize = 209715200;

    static const uint64_t encSectionSize = 256;
    static std::wstring separator = std::wstring(1, std::filesystem::path::preferred_separator);
    static const int miningReward = 1;
}

namespace Packets {
    struct ResponseDfsSize {
        std::string Actor;
        uint64_t Size;

        MSGPACK_DEFINE(Actor, Size)
    };

    struct RequestDfsSize {
        std::string Actor;

        MSGPACK_DEFINE(Actor)
    };

    struct AddFileMessage {
        std::string Actor;
        std::string FileName;
        std::string FileHash;
        std::string Path;
        uint64_t Size;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Path, Size)
    };

    struct RequestFileSegmentMessage {
        std::string Actor;
        std::string FileName;
        std::string FileHash;
        std::string Path;
        uint64_t Offset;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Path, Offset)
    };

    struct RemoveFileMessage {
        std::string Actor;
        std::string FileName;
        MSGPACK_DEFINE(Actor, FileName)
    };

    struct SegmentMessage {
        std::string Actor;
        std::string FileName;
        std::string FileHash;
        std::string Data;
        uint64_t Offset;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Data, Offset)
    };
    enum SegmentMessageType {
        add = 0,
        insert = 1,
        replace = 2,
        remove = 3
    };

    struct EditSegmentMessage {
        std::string Actor;
        std::string FileName;
        std::string FileHash;
        std::string NewFileHash;
        std::string Data;
        uint64_t Offset;
        SegmentMessageType ActionType;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Data, Offset, ActionType)
    };

    struct DeleteSegmentMessage {
        std::string Actor;
        std::string FileName;
        std::string FileHash;
        uint64_t Offset;
        uint64_t Size;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Offset, Size)
    };

    struct DirRow {
        std::string fileHash;
        std::string fileHashPrev;
        std::string filePath;
        std::string fileName;
        uint64_t fileSize;
        uint64_t lastModified;
        MSGPACK_DEFINE(fileHash, fileHashPrev, filePath, fileName, fileSize, lastModified)
    };

    struct VerifyFileMessage {
        std::string Actor;
        std::string FileHash;
        std::string FileName;
        bool Verified = false;
        uint64_t Size;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Verified, Size)
    };

    enum StateMessageType {
        base = 0
    };

    struct StateMessage {
        StateMessageType StateTypeMessage;
        uint64_t DataAmountStored;
        double Coefficient = 0.5;
        double CoinProducedForNode = 0.0;
        uint64_t CoinProductionAlgorithmTickBlocks = 100;
        float BlockProductionRate = 0.5;
        uint64_t CoinProductionAlgorithmTickPerHour = 18;

        MSGPACK_DEFINE(StateTypeMessage, DataAmountStored, Coefficient, CoinProducedForNode,
                       CoinProductionAlgorithmTickBlocks, BlockProductionRate,
                       CoinProductionAlgorithmTickPerHour)
    };
}
namespace Fragments {
    static const std::string Extension = ".storj";
    static const std::string TableNameFragments = "Fragments";
    static const std::string CreateTableQueryFragments = "CREATE TABLE IF NOT EXISTS " + TableNameFragments
        + "("
          "pos        INTEGER PRIMARY KEY NOT NULL, "
          "storedPos  INTEGER             NOT NULL, "
          "size       INTEGER             NOT NULL, "
          "fragHash   TEXT                NOT NULL"
          ");";

    struct FragmentsInfo {
        std::string actor;
        std::string fileHash;
        std::string filePath;
        uint64_t fileSize;
        std::list<std::pair<int, int>> fragmentPositionList;

        void print() const {
            qDebug() << "actor: [" << actor.c_str() << "]"
                     << "fileHash" << fileHash.c_str() << "]"
                     << "filePath" << filePath.c_str() << "]";
            for (const auto &pair : fragmentPositionList) {
                qDebug() << pair.first << pair.second;
            }
        }
        MSGPACK_DEFINE(actor, fileHash, filePath, fileSize, fragmentPositionList)
    };
}
namespace Historical {
    struct FileChange {
        uint64_t pos;
        std::string data;

        std::string toStdString() {
            return Tools::typeToStdStringBytes<uint64_t>(pos) + data;
        }
        void fromStdString(std::string string) {
            pos = Tools::stdStringBytesToType<uint64_t>(string.substr(0, 8));
            data = string.substr(8);
        }
    };
    static const std::string TableNameHC = "HistoricalChain";
    static const std::string CreateTableHistoricalChain = "CREATE TABLE IF NOT EXISTS " + TableNameHC
        + "("
          "num        INTEGER PRIMARY KEY NOT NULL,"
          "prevNum    INTEGER             NOT NULL,"
          "type       INTEGER             NOT NULL,"
          "data       BLOB                NOT NULL,"
          "hash       TEXT                NOT NULL "
          ");";
}
namespace Reward {
    static const BigNumber coinProductionAlgorithmTick = BigNumber("100", 10);
    struct CoinReward {
        std::string Actor;
        int Coin;
        MSGPACK_DEFINE(Actor, Coin)
    };
}

namespace Tables {
    namespace ActorDirFile {
        static const std::string TableName = "FilesTable";
        static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
            + "("
              "fileName     TEXT PRIMARY KEY NOT NULL,"
              "fileNamePrev TEXT             NOT NULL,"
              "fileHash     TEXT             NOT NULL,"
              "filePath     TEXT             NOT NULL,"
              "fileSize     INTEGER          NOT NULL,"
              "lastModified INTEGER          NOT NULL "
              ");";
        std::vector<DBRow> getFileDataByHash(DBConnector *db, std::string hash);
        std::vector<DBRow> getFileDataByName(DBConnector *db, std::string name);
        std::string getLastName(DBConnector &db);
        int totalFileSize(const std::string &actorId);
        uint64_t dataAmountStoredSize(const std::string &actorId, const std::string &storjName);

        // TODO: optional
        DBConnector actorDbConnector(const std::string &actorId);
        std::filesystem::path actorDbPath(const std::string &actorId);
        std::filesystem::path storjDbPath(const std::string &actorId, const std::string &storjName);
        DFS::Packets::DirRow getDirRow(const std::string &actorId, const std::string &fileHash);
        std::vector<DFS::Packets::DirRow> getDirRows(const std::string &actorId, uint64_t lastModified = 0);
        bool addDirRows(const std::string &actorId, const std::vector<DFS::Packets::DirRow> &dirRows);
    }

    namespace DirsFile {
        static const std::string TableName = "Dirs";
        static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
            + "("
              "actorId      TEXT PRIMARY KEY NOT NULL,"
              "lastModified INTEGER          NOT NULL "
              ");";
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
        "SELECT * FROM " + DFS::Tables::ActorDirFile::TableName + " ORDER BY fileName DESC LIMIT 1";
    static const std::string filesTableFull = "SELECT * FROM " + DFS::Tables::ActorDirFile::TableName;
}

namespace Path {
    std::filesystem::path convertPathToPlatform(const std::filesystem::path &path);
    std::filesystem::path filePath(const ActorId &actorId, const std::string &fileName);
}

namespace Balances {
    const std::string balanceDbPath = "blockchain/balance.db";
    const std::string balancesTableName = "balances";
    const std::string createBalanceTable = "CREATE TABLE IF NOT EXISTS balances("
                                           "actor_id       TEXT   NOT NULL, "
                                           "balance       TEXT   NOT NULL, "
                                           "last_update    TEXT   NOT NULL );";
    const std::string loadBalancesQuery = "SELECT * FROM balances";

    struct Balance {
        std::string Actor;
        std::string Balance;
    };
}

enum class Encryption {
    Public = 0,
    Encrypted = 1
};
}
namespace DFSP = DFS::Packets;
namespace DFSF = DFS::Fragments;
namespace DFST = DFS::Tables;
namespace STDFS = std::filesystem;
namespace DFSHC = DFS::Historical;
namespace DFSB = DFS::Basic;
namespace DFS_PATH = DFS::Path;
namespace DFSR = DFS::Reward;

MSGPACK_ADD_ENUM(DFS::Packets::SegmentMessageType)
MSGPACK_ADD_ENUM(DFS::Packets::StateMessageType)

#endif // DFS_UTILS_H
