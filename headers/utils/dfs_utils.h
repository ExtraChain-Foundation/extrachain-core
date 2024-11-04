#ifndef DFS_UTILS_H
#define DFS_UTILS_H

#include <filesystem>
#include <string>
#include <vector>

#include "datastorage/actor.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/db_connector.h"
#include <boost/algorithm/string/replace.hpp>
#include <fmt/format.h>
#include <msgpack.hpp>

#include "utils/exc_logs.h"
#include "utils/exc_magic.h"
#include "utils/exc_msgpack_describe.h"

namespace Tools {
template <typename T>
std::vector<unsigned char> typeToByteArray(T integerValue) {
    std::vector<unsigned char> res;
    unsigned char*             b = (unsigned char*)(&integerValue);
    unsigned char*             e = b + sizeof(T);
    std::copy(b, e, back_inserter(res));
    return res;
}

template <typename T>
T byteArrayToType(std::vector<unsigned char> value) {
    T* res;
    res = reinterpret_cast<T*>(value.data());
    return *res;
}

template <typename T>
std::string typeToStdStringBytes(T integerValue) {
    std::string    res;
    unsigned char* b = (unsigned char*)(&integerValue);
    unsigned char* e = b + sizeof(T);
    std::copy(b, e, back_inserter(res));
    return res;
}

template <typename T>
T stdStringBytesToType(std::string value) {
    T* res;
    res = reinterpret_cast<T*>(value.data());
    return *res;
}
}

namespace Utils {
std::string platformDelimeter();

inline static std::uint64_t globalVariableOfDfsSize = 0;
}

namespace DFS {
namespace Basic {
    static const std::string   fsActrRoot                 = "dfs";
    static const std::wstring  fsActrRootW                = L"dfs";
    static const std::string   fsMapName                  = ".dir";
    static const std::string   dirsPath                   = "dfs/.dirs";
    static const std::uint64_t sectionSize                = /*2097152*/ 524228;
    static const std::uint64_t maxSectionSize             = 209715200;
    static const std::uint64_t minDfsLimit                = 2147483648;
    static const std::uint64_t historicalChainSectionSize = 209715200;

    static const std::uint64_t encSectionSize   = 256;
    static std::wstring        separator        = std::wstring(1, std::filesystem::path::preferred_separator);
    static const int           miningReward     = 1;
    static const std::string   dsStoreExtention = ".DS_Store";
}

enum class FileType {
    Folder   = 0,
    Bytes    = 1,
    Database = 2
};

enum class FileState {
    Unloaded  = 0,
    Partially = 1,
    Loaded    = 2
};

enum class Encryption {
    Public    = 0,
    Encrypted = 1
};

struct DirRow {
    ActorId actorId;

    std::string                fileId;
    std::optional<std::string> fileIdPrev;

    std::string hash;

    std::optional<std::string> folder;
    std::string                name;

    std::size_t   size;
    std::uint64_t created;
    std::uint64_t lastModified;

    DFS::FileType   type;
    DFS::Encryption encryption;
    DFS::FileState  state;

    std::string visualPath() const {
        if (folder.has_value())
            return folder.value() + "/" + name;
        else
            return name;
    }

    bool isLoaded() const {
        return state == DFS::FileState::Loaded;
    }

    bool isEncrypted() const {
        return encryption == DFS::Encryption::Encrypted;
    }
};

BOOST_DESCRIBE_STRUCT(
    DirRow,
    (),
    (actorId, fileId, fileIdPrev, hash, folder, name, size, created, lastModified, type, encryption, state))

MAKE_MAGICAL_OPERATORS(DirRow)

namespace Packets {
    struct ResponseDfsSize {
        ActorId     Actor;
        std::size_t Size;

        MSGPACK_DEFINE(Actor, Size)
    };

    struct RequestDfsSize {
        ActorId Actor;

        MSGPACK_DEFINE(Actor)
    };

    struct ResponseBlockCount {
        ActorId   Actor;
        BigNumber blockCount;

        MSGPACK_DEFINE(Actor, blockCount)
    };

    struct RequestBlockCount {
        ActorId Actor;

        MSGPACK_DEFINE(Actor)
    };

    struct RequestFileSegmentMessage {
        ActorId       Actor;
        std::string   FileName;
        std::string   FileHash;
        std::string   Path;
        std::uint64_t Offset;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Path, Offset)
    };

    struct RemoveFileMessage {
        ActorId     Actor;
        std::string FileName;
        MSGPACK_DEFINE(Actor, FileName)
    };

    struct SegmentMessage {
        ActorId       Actor;
        std::string   FileName;
        std::string   FileHash;
        std::string   Data;
        std::uint64_t Offset;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Data, Offset)
    };

    enum SegmentMessageType {
        add     = 0,
        insert  = 1,
        replace = 2,
        remove  = 3
    };

    struct EditSegmentMessage {
        ActorId            Actor;
        std::string        FileName;
        std::string        FileHash;
        std::string        NewFileHash;
        std::string        Data;
        std::uint64_t      Offset;
        SegmentMessageType ActionType;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Data, Offset, ActionType)
    };

    struct DeleteSegmentMessage {
        ActorId       Actor;
        std::string   FileName;
        std::string   FileHash;
        std::uint64_t Offset;
        std::uint64_t Size;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Offset, Size)
    };

    struct VerifyFileMessage {
        ActorId       Actor;
        std::string   FileHash;
        std::string   FileName;
        bool          Verified = false;
        std::uint64_t Size;
        MSGPACK_DEFINE(Actor, FileName, FileHash, Verified, Size)
    };

    struct Connection {
        std::string port;
        std::string address;
        bool        active;
        MSGPACK_DEFINE(port, address, active)
    };

    struct Activity {
        std::uint64_t timeactivity;
        bool          active;
        std::uint64_t score;
        MSGPACK_DEFINE(timeactivity, active, score)
    };

    struct WSConnection {
        std::string   address;
        std::uint64_t port;
        MSGPACK_DEFINE(address, port)
    };

    struct ReferenceData {
        std::string key;
        std::string access;

        ReferenceData() {
        }

        ReferenceData(std::string _key, std::string _access)
            : key(_key)
            , access(_access) { };

        std::string toString() const {
            return std::string(fmt::format("[\"key\":\"{}\",\"access\":\"{}\"]", key, access));
        }
    };
}

namespace Fragments {
    static const std::string Extension          = ".storj";
    static const std::string ExtensionJournal   = ".storj-journal";
    static const std::string TableNameFragments = "Fragments";
    static const std::string CreateTableQueryFragments = "CREATE TABLE IF NOT EXISTS " + TableNameFragments
                                                         + "("
                                                         "pos        INTEGER PRIMARY KEY NOT NULL, "
                                                         "storedPos  INTEGER             NOT NULL, "
                                                         "size       INTEGER             NOT NULL, "
                                                         "fragHash   TEXT                NOT NULL"
                                                         ");";

    static const std::string GetCountFragmants = "SELECT COUNT(size) FROM Fragments";
    static const std::string GetSizeFragmants  = "SELECT SUM(size) FROM Fragments";

    struct FragmentsInfo {
        ActorId                        actor;
        std::string                    fileHash;
        std::string                    filePath;
        std::uint64_t                  fileSize;
        std::list<std::pair<int, int>> fragmentPositionList;

        void print() const {
            qDebug() << "actor: [" << actor << "]"
                     << "fileHash" << fileHash.c_str() << "]"
                     << "filePath" << filePath.c_str() << "]";
            for (const auto& pair : fragmentPositionList) {
                qDebug() << pair.first << pair.second;
            }
        }

        MSGPACK_DEFINE(actor, fileHash, filePath, fileSize, fragmentPositionList)
    };
}

namespace Historical {
    struct FileChange {
        std::uint64_t pos;
        std::string   data;

        std::string toStdString() {
            return Tools::typeToStdStringBytes<std::uint64_t>(pos) + data;
        }

        void fromStdString(std::string string) {
            pos  = Tools::stdStringBytesToType<std::uint64_t>(string.substr(0, 8));
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
    static const BigNumber coinProductionAlgorithmTick = BigNumber("20", NumeralBase::Dec); // 100
    struct CoinReward {
        ActorId        Actor;
        BigNumberFloat Coin;
        MSGPACK_DEFINE(Actor, Coin)
    };

    enum TypeFunctioning {
        Base,
        Test
    };

    struct RequestReward {
        ActorId         Actor;
        std::uint64_t   DataStoredSize;
        TypeFunctioning TypeFunctioningObj;
        BigNumberFloat  RewardAmount;
        std::uint64_t   BytesSent;
        std::uint64_t   BytesReceived;
        BigNumber       BlocksStored;
        MSGPACK_DEFINE(
            Actor,
            DataStoredSize,
            TypeFunctioningObj,
            RewardAmount,
            BytesSent,
            BytesReceived,
            BlocksStored)
    };
}

namespace Tables {
    namespace ActorDirFile {
        static const std::string TableName = "Files";
        static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
                                                    + "("
                                                    "fileId            TEXT PRIMARY KEY NOT NULL,"
                                                    "fileIdPrev        TEXT             NOT NULL,"
                                                    "hash          TEXT             NOT NULL,"
                                                    "folder        TEXT             NOT NULL,"
                                                    "name          TEXT             NOT NULL,"
                                                    "size          INTEGER          NOT NULL,"
                                                    "lastModified  INTEGER          NOT NULL,"
                                                    "state         INTEGER          NOT NULL CHECK (state BETWEEN 0 AND 2)"
                                                    ");";
        std::vector<DBRow> getFileDataByHash(DBConnector* db, std::string hash);
        std::vector<DBRow> getFileDataByName(DBConnector* db, std::string name);
        std::string        getLastName(DBConnector& db);
        int                totalFileSize(const ActorId& actorId);
        std::uint64_t      dataAmountStoredSize(const ActorId& actorId, const std::string& storjName);

        // TODO: optional
        DBConnector              actorDbConnector(const ActorId& actorId);
        std::filesystem::path    actorDbPath(const ActorId& actorId);
        std::filesystem::path    storjDbPath(const ActorId& actorId, const std::string& storjName);
        DFS::DirRow              getDirRow(const ActorId& actorId, const std::string& fileId);
        std::vector<DFS::DirRow> getDirRows(const ActorId& actorId, std::uint64_t lastModified = 0);
        bool                     addDirRows(const ActorId& actorId, const std::vector<DFS::DirRow>& dirRows);
    }

    namespace DirsFile {
        static const std::string TableName = "Dirs";
        static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS " + TableName
                                                    + "("
                                                    "actorId      TEXT PRIMARY KEY NOT NULL,"
                                                    "lastModified INTEGER          NOT NULL "
                                                    ");";
        static const std::string ParametersDfs              = "Parameters";
        static const std::string CreateParametersTableQuery = fmt::format(
            "CREATE TABLE IF NOT EXISTS {}("
            "parameter    TEXT    NOT NULL, "
            "value        TEXT    NOT NULL) ",
            ParametersDfs);
        static const std::string BytesLimit = "bytes_limit";
        static const std::string BytesLimitQuery =
            fmt::format("SELECT * FROM {} WHERE parameter = '{}'", ParametersDfs, BytesLimit);
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
    std::filesystem::path convertPathToPlatform(const std::filesystem::path& path);
    std::filesystem::path filePath(const ActorId& actorId, const std::string& fileName);
    std::filesystem::path actorPath(const ActorId& actorId);
}

namespace Balances {
    const std::string balanceDbPath     = "blockchain/balance.db";
    const std::string balancesTableName = "balances";
    const std::string createBalanceTable =
        "CREATE TABLE IF NOT EXISTS balances("
        "actor_id       TEXT   NOT NULL, "
        "balance       TEXT   NOT NULL, "
        "last_update    TEXT   NOT NULL );";
    const std::string loadBalancesQuery = "SELECT * FROM balances";

    struct Balance {
        ActorId     actor;
        std::string balance = "";
    };
}
}

namespace DFSP     = DFS::Packets;
namespace DFSF     = DFS::Fragments;
namespace DFST     = DFS::Tables;
namespace STDFS    = std::filesystem;
namespace DFSHC    = DFS::Historical;
namespace DFSB     = DFS::Basic;
namespace DFS_PATH = DFS::Path;
namespace DFSR     = DFS::Reward;

MAKE_MAGICAL_FORMATTER(DFS::DirRow)

FORMAT_ENUM(DFS::FileType)
FORMAT_ENUM(DFS::FileState)
FORMAT_ENUM(DFS::Encryption)
FORMAT_ENUM(DFS::Packets::SegmentMessageType)
FORMAT_ENUM(DFS::Reward::TypeFunctioning)

MSGPACK_ADD_ENUM(DFS::FileType)
MSGPACK_ADD_ENUM(DFS::FileState)
MSGPACK_ADD_ENUM(DFS::Encryption)
MSGPACK_ADD_ENUM(DFS::Packets::SegmentMessageType)
MSGPACK_ADD_ENUM(DFS::Reward::TypeFunctioning)

#endif // DFS_UTILS_H
