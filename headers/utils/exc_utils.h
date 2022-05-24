/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef UTILS_H
#define UTILS_H

#include <sstream>
#include <string>
#include <vector>

#include <QFile>
#include <QObject>
#include <QtNetwork/QNetworkAddressEntry>

#include "extrachain_global.h"
#include <msgpack.hpp>

#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/os.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <magic_enum.hpp>
using namespace magic_enum::ostream_operators;
using namespace magic_enum::bitwise_operators;

#define FORMAT_ENUM(E)                                        \
    template <>                                               \
    struct fmt::formatter<E> : formatter<string_view> {       \
        template <typename FormatContext>                     \
        auto format(E Enum, FormatContext &ctx) {             \
            static_assert(std::is_enum_v<E>);                 \
            string_view name = "unknown";                     \
            name = magic_enum::enum_name(Enum);               \
            return formatter<string_view>::format(name, ctx); \
        }                                                     \
    };

namespace fmt {
template <typename... Args>
void println(fmt::format_string<Args...> &&fmt_str, Args &&...args) {
    fmt::print("{}\n",
               fmt::format(std::forward<fmt::format_string<Args...>>(fmt_str), std::forward<Args>(args)...));
}
}

namespace Network {
Q_NAMESPACE

static bool isStartedServer = true;
static quint16 maxConnections = 100;
static bool networkDebug = false;

enum class Protocol {
    Undefined = 0,
    Udp = 1,
    WebSocket = 2
};
Q_ENUM_NS(Protocol)

enum class SocketServiceError {
    Unknown = 0,
    IncompatibleVersion = 1,
    IncompatibleNetwork = 2,
    IncompatibleIdentifier = 3,
    DuplicateIdentifier = 4,
};
Q_ENUM_NS(SocketServiceError)

[[maybe_unused]] inline static QByteArray currentIdentifier() {
    // static QByteArray identifier;
    // if (!identifier.isEmpty())
    //     return identifier;

    QFile file(".settings");
    file.open(QIODevice::ReadOnly);
    QByteArray identifier = file.readAll();
    file.close();
    return identifier;
}
} // namespace Network

namespace Config {

// Message pattern for qDebug (see
// http://doc.qt.io/qt-5/qtglobal.html#qSetMessagePattern)
// const QString MESSAGE_PATTERN = "[%{time h:mm:ss.zzz}][%{function}][%{type}]: %{message}";

const int NECESSARY_SAME_TX = 1;

namespace DataStorage {
    static const std::string cardTableName = "Items";
    static const std::string cardDeletedTableName = "ItemsDeleted";
    static const std::string cardTableFields = " ("
                                               "key     INTEGER NOT NULL,"
                                               "id      TEXT  NOT NULL, "
                                               "type    INT   NOT NULL, "
                                               "prevId  TEXT  NOT NULL, "
                                               "nextId  TEXT  NOT NULL, "
                                               "version INT   NOT NULL, "
                                               "sign    TEXT  NOT NULL, "
                                               "PRIMARY KEY (id, type)"
                                               ");";
    static const std::string cardTableCreation =
        "CREATE TABLE IF NOT EXISTS " + cardTableName + cardTableFields;
    static const std::string cardDeletedTableCreation =
        "CREATE TABLE IF NOT EXISTS " + cardDeletedTableName + cardTableFields;

    static const std::string userNameTableName = "Usernames";
    static const std::string userNameTableCreation = "CREATE TABLE IF NOT EXISTS " + userNameTableName
        + " ("
          "actorId  TEXT PRIMARY KEY NOT NULL, "
          "username TEXT             NOT NULL, "
          "sign     TEXT             NOT NULL );";

    static const std::string chatIdTableName = "ChatId";
    static const std::string chatIdStorage = "CREATE TABLE IF NOT EXISTS " + chatIdTableName
        + " ("
          "chatId  BLOB  PRIMARY KEY NOT NULL, "
          "key     BLOB              NOT NULL, "
          "owner   BLOB              NOT NULL );";

    static const std::string chatUserTableName = "Users";
    static const std::string chatUserStorage = "CREATE TABLE IF NOT EXISTS " + chatUserTableName
        + " ("
          "userId  TEXT  PRIMARY KEY NOT NULL);";

    static const std::string chatMessageTableName = "Chat";
    static const std::string sessionChatMessageStorage = "CREATE TABLE IF NOT EXISTS " + chatMessageTableName
        + " ("
          "messId   TEXT PRIMARY KEY NOT NULL, "
          "userId   TEXT             NOT NULL, "
          "message  BLOB             NOT NULL, "
          "type     TEXT             NOT NULL, "
          "date     INTEGER          NOT NULL );";

    static const std::string storedTableName = "Stored";
    static const std::string storedTableCreation = "CREATE TABLE IF NOT EXISTS " + storedTableName
        + " ("
          "hash      TEXT PRIMARY KEY NOT NULL, "
          "data      BLOB             NOT NULL, "
          "range     TEXT             NOT NULL, "
          "type      INT              NOT NULL, "
          "userId    TEXT             NOT NULL, "
          "version   INT              NOT NULL, "
          "prevHash  TEXT             NOT NULL,"
          "sign      TEXT             NOT NULL"
          ");";

    static const std::string subscribeFollowerTableName = "Subscribers";
    static const std::string tableFollowerCreation = "CREATE TABLE IF NOT EXISTS "
        + subscribeFollowerTableName
        + " ("
          "subscriber    TEXT PRIMARY KEY NOT NULL,"
          "sign TEXT             NOT NULL)";
    static const std::string subscribeColumnTableName = "Subscriptions";
    static const std::string tableMySubscribeCreation = "CREATE TABLE IF NOT EXISTS "
        + subscribeColumnTableName
        + " ("
          "subscription    TEXT PRIMARY KEY NOT NULL);";

    static const std::string chatInviteTableName = "Invite";
    static const std::string chatInviteCreation = "CREATE TABLE IF NOT EXISTS " + chatInviteTableName
        + " ("
          "chatId  BLOB PRIMARY KEY NOT NULL, "
          "message BLOB             NOT NULL, "
          "owner   BLOB             NOT NULL  );";

    static const std::string notificationTable = "Notification";
    static const std::string notificationTableCreation = "CREATE TABLE IF NOT EXISTS " + notificationTable
        + " ("
          "time  BLOB PRIMARY KEY NOT NULL, "
          "type  BLOB                 NOT NULL, "
          "data  BLOB                NOT NULL  );";

    static const std::string propertiesTableName = "Properties";
    static const std::string usersDBAddition = ".users";
    static const std::string usersSubscribedOnEventTable = "Users";
    static const std::string textTableName = "Text";
    static const std::string attachTableName = "Attachments";
    static const std::string commentsTableName = "Comments";
    static const std::string commentsLikesTableName = "Likes";
    static const std::string likesTableName = "Likes";
    static const std::string savedPostsTableName = "Posts";
    static const std::string savedEventsTableName = "Events";
    static const std::string usersMarkedTableName = "UsersMarked";

    static const std::string propertiesFields = " ("
                                                "version    TEXT    NOT NULL,"
                                                "sender     TEXT    NOT NULL,"
                                                "dateCreate INTEGER NOT NULL,"
                                                "dateModify INTEGER NOT NULL,"
                                                "latitude   REAL NOT NULL,"
                                                "longitude  REAL NOT NULL,";

    static const std::string postPropertiesTableCreation = "CREATE TABLE IF NOT EXISTS " + propertiesTableName
        + propertiesFields
        + "sign         TEXT     NOT NULL"
          ");";

    static const std::string userListPropertiesTableCreation = "CREATE TABLE IF NOT EXISTS "
        + propertiesTableName
        + " ("
          "eventId TEXT NOT NULL"
          ");";

    static const std::string eventUserDbProperties = "CREATE TABLE IF NOT EXISTS "
        + usersSubscribedOnEventTable
        + " ("
          "userId       TEXT PRIMARY KEY NOT NULL,"
          "sign         TEXT                 NULL,"
          "approver     TEXT                 NULL,"
          "approverSign TEXT                 NULL "
          ");";

    static const std::string eventPropertiesTableCreation = "CREATE TABLE IF NOT EXISTS "
        + propertiesTableName + propertiesFields
        + "eventName    TEXT    NOT NULL,"
          "type         TEXT    NOT NULL,"
          "locationName TEXT    NOT NULL,"
          "scope        TEXT    NOT NULL,"
          "agreement    INT     NOT NULL,"
          "salary       TEXT    NOT NULL,"
          "startEpoch   INTEGER NOT NULL,"
          "endEpoch     INTEGER NOT NULL,"
          "start        TEXT    NOT NULL,"
          "end          TEXT    NOT NULL,"
          "sign         TEXT    NOT NULL"
          ");";

    static const std::string textTableCreation = "CREATE TABLE IF NOT EXISTS " + textTableName
        + " ("
          "locale TEXT PRIMARY KEY NOT NULL, "
          "text   TEXT             NOT NULL, "
          "sign   TEXT             NOT NULL  "
          ");";

    static const std::string attachTableCreation = "CREATE TABLE IF NOT EXISTS " + attachTableName
        + " ("
          "attachId   TEXT       NOT NULL,"
          "type       TEXT       NOT NULL, " // image, gif, video, event, post
          "date       INTEGER    NOT NULL, "
          "data       TEXT       NOT NULL, "
          "sign       TEXT       NOT NULL  "
          ");";

    static const std::string commentsTableCreation = "CREATE TABLE IF NOT EXISTS " + commentsTableName
        + " ("
          "commentId  TEXT  PRIMARY KEY  NOT NULL, "
          "sender     TEXT               NOT NULL, "
          "message    BLOB               NOT NULL, "
          "date       TEXT               NOT NULL, "
          "sub        TEXT               NULL, "
          "sign       TEXT               NOT NULL  "
          ");";
    static const std::string commentsLikesTableCreation = "CREATE TABLE IF NOT EXISTS "
        + commentsLikesTableName
        + " ("
          "commentId TEXT NOT NULL,"
          "userId    TEXT NOT NULL,"
          "sign      TEXT NOT NULL,"
          "PRIMARY KEY (commentId, userId)"
          ");";

    static const std::string likesTableCreation = "CREATE TABLE IF NOT EXISTS " + likesTableName
        + " ("
          "userId TEXT PRIMARY KEY NOT NULL,"
          "sign   TEXT            NOT NULL);";

    static const std::string savedPostsTableCreation = "CREATE TABLE IF NOT EXISTS " + savedPostsTableName
        + " ("
          "user BLOB NOT NULL,"
          "post BLOB NOT NULL,"
          "PRIMARY KEY (user, post)"
          ");";
    static const std::string likedEventsTableCreation = "CREATE TABLE IF NOT EXISTS " + savedEventsTableName
        + " ("
          "user  BLOB NOT NULL,"
          "event BLOB NOT NULL,"
          "PRIMARY KEY (user, event)"
          ");";
    static const std::string savedEventsTableCreation = "CREATE TABLE IF NOT EXISTS " + savedEventsTableName
        + " ("
          "user       BLOB NOT NULL,"
          "event      BLOB NOT NULL,"
          "name       BLOB NOT NULL,"
          "start      BLOB NOT NULL,"
          "end        BLOB NOT NULL,"
          "PRIMARY KEY (user, event)"
          ");";

    static const std::string usersMarkedTableCreation = "CREATE TABLE IF NOT EXISTS " + usersMarkedTableName
        + " ("
          "userId TEXT PRIMARY KEY NOT NULL,"
          "sign  TEXT            NOT NULL);";

    static const std::string BlockTable = "Block";
    static const std::string BlockTableCreate = "CREATE TABLE IF NOT EXISTS " + BlockTable
        + " ( "
          "type         TEXT  NOT NULL, "
          "id           TEXT  NOT NULL, "
          "date         TEXT  NOT NULL, "
          "data         TEXT          , "
          "prevHash     TEXT  NOT NULL, "
          "hash         TEXT  NOT NULL  "
          ");";
    static const std::string TxBlockTable = "Transactions";
    static const std::string TxBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + TxBlockTable
        + " ("
          "sender       TEXT  NOT NULL, "
          "receiver     TEXT  NOT NULL, "
          "amount       TEXT  NOT NULL, "
          "date         TEXT  NOT NULL, "
          "data         TEXT          , "
          "token        TEXT  NOT NULL, "
          "prevBlock    TEXT  NOT NULL, "
          "gas          TEXT  NOT NULL, "
          "hop          TEXT  NOT NULL, "
          "hash         TEXT  NOT NULL, "
          "approver     TEXT  NOT NULL, "
          "digSig       TEXT  NOT NULL, "
          "producer     TEXT  NOT NULL "
          ");";
    static const std::string SignTable = "Signatures";
    static const std::string SignBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + SignTable
        + " ("
          "actorId      TEXT PRIMARY KEY NOT NULL, "
          "digSig       TEXT             NOT NULL, "
          "type         TEXT             NOT NULL  "
          ");";

    static const std::string GenesisBlockTable = "GenesisBlock";
    static const std::string GenesisBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + GenesisBlockTable
        + " ("
          "type         TEXT  NOT NULL, "
          "id           TEXT  NOT NULL, "
          "date         TEXT  NOT NULL, "
          "data         TEXT          , "
          "prevHash     TEXT  NOT NULL, "
          "hash         TEXT  NOT NULL, "
          "prevGenHash  TEXT            "
          ");";
    static const std::string RowGenesisBlockTable = "GenesisDataRow";
    static const std::string RowGenesisBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + RowGenesisBlockTable
        + " ("
          "actorId    TEXT  NOT NULL, "
          "state      TEXT  NOT NULL, "
          "token      TEXT  NOT NULL, "
          "type       TEXT  NOT NULL "
          ");";

    static const std::string tokensCacheTable = "Tokens";
    static const std::string tokensCacheTableCreate = "CREATE TABLE IF NOT EXISTS " + tokensCacheTable
        + " ("
          "tokenId      TEXT PRIMARY KEY NOT NULL, "
          "name         TEXT             NOT NULL, "
          "color        TEXT             NOT NULL, "
          "canStaking   INT              NOT NULL  "
          ");";

    static const std::string actorsTable = "Actors";
    static const std::string actorsTableCreate = "CREATE TABLE IF NOT EXISTS " + actorsTable
        + " ("
          "id   TEXT PRIMARY KEY NOT NULL, "
          "type INT              NOT NULL  "
          ");";

    //    static const std::string filesTable = "FilesTable";
    //    static const std::string filesTableCreate = "CREATE TABLE IF NOT EXISTS " + filesTable
    //        + " ("
    //          "fileHash     TEXT PRIMARY KEY NOT NULL, "
    //          "fileHashPrev TEXT             NOT NULL, "
    //          "filePath     TEXT             NOT NULL, "
    //          "fileSize     TEXT             NOT NULL"
    //          ");";

    //    static const std::string fileSegmentsTable = "FileSegmentsTable";
    //    static const std::string fileSegmentsTableCreate = "CREATE TABLE IF NOT EXISTS " + fileSegmentsTable
    //        + " ("
    //          "fileHash         TEXT PRIMARY KEY NOT NULL, "
    //          "fileHashPrev     TEXT             NOT NULL, "
    //          "filePath         TEXT             NOT NULL, "
    //          "fileSegmentBegin TEXT             NOT NULL, "
    //          "fileSegmentEnd   TEXT             NOT NULL, "
    //          "fileSize         TEXT             NOT NULL"
    //          ");";

    //    static const std::string permissionTable = "PermissionTable";
    //    static const std::string permissionTableCreate = "CREATE TABLE IF NOT EXISTS " + permissionTable
    //        + " ("
    //          "fileHash   TEXT NOT NULL, "
    //          "permission TEXT NOT NULL, "
    //          "userId     TEXT NOT NULL,"
    //          "signature  TEXT NOT NULL"
    //          ");";

    //    static const std::string filesTableLast = "SELECT * FROM " + filesTable + " ORDER BY fileHash DESC
    //    LIMIT 1"; static const std::string filesTableFull = "SELECT * FROM " + filesTable;

    // How many files one section folder will store
    static const int SECTION_SIZE = 1000;

    // How often to construct block from pending transactions (in miliseconds)
    static const int BLOCK_CREATION_PERIOD = 1000;

    // How often to construct genesis block (in blocks)
    static const int CONSTRUCT_GENESIS_EVERY_BLOCKS = 100;

    // Max number of saved blocks in mem index
    static const int MEM_INDEX_SIZE_LIMIT = 1000;
} // namespace DataStorage

namespace Net {
    // Default gas for transaction
    static const int DEFAULT_GAS = 10;

    // Networking will work only if there are enough peers
    static const int MINIMUM_PEERS = 1;

    // Get Message is considered successful only after NECESSARY_RESPONSE_COUNT
    // responses
    static const int NECESSARY_RESPONSE_COUNT = 1; // 3

    enum class TypeSend {
        All,
        Except,
        Focused
    };
} // namespace Net
} // namespace Config
MSGPACK_ADD_ENUM(Config::Net::TypeSend)
FORMAT_ENUM(Config::Net::TypeSend)

namespace Errors {
// IO
static const int FILE_ALREADY_EXISTS = 101;
static const int FILE_IS_NOT_OPENED = 102;

// Blocks
static const int BLOCK_IS_NOT_VALID = 201;
static const int BLOCKS_CANT_MERGE = 202;
static const int BLOCKS_ARE_EQUAL = 203;

// Mem and Block index
static const int NO_BLOCKS = 401;
} // namespace Errors

namespace Serialization {

// Delimiters //
static const int TRANSACTION_FIELD_SIZE = 4;
static const int DEFAULT_FIELD_SIZE = 8;

EXTRACHAIN_EXPORT QByteArray serialize(const QList<QByteArray> &list,
                                       const int &fiels_size = DEFAULT_FIELD_SIZE);
EXTRACHAIN_EXPORT QList<QByteArray> deserialize(const QByteArray &serialized,
                                                const int &fiels_size = DEFAULT_FIELD_SIZE);

bool isEmpty(const QByteArray &bytes);
bool isEmpty(const std::string &str);
bool isEmpty(std::string_view str_view);
} // namespace Seralization

namespace MessagePack {
template <class T>
std::string serialize(const T &t) {
    std::stringstream ss;
    msgpack::pack(ss, t);
    ss.seekg(0);
    return ss.str();
}

template <class T, class StringContainer>
T deserialize(const StringContainer &data, std::size_t size = 0) {
    if (Serialization::isEmpty(data)) {
        qDebug() << "[MessagePack] Empty deserialize" << typeid(T).name();
        qFatal("[MessagePack] Empty deserialize");
        return T();
    }

    try {
        msgpack::object_handle oh = msgpack::unpack(data.data(), data.size());
        msgpack::object deserialized = oh.get();
        auto t = deserialized.as<T>();
        return t;
    } catch (std::exception &e) { }

    auto qt_bytes = QByteArray::fromStdString(data.data());
    qDebug() << "[MessagePack] Incorrect deserialize for" << qt_bytes.toBase64() << qt_bytes;
    qFatal("[MessagePack] Incorrect deserialize");
    return T();
}
} // namespace MessagePack

namespace Utils {
EXTRACHAIN_EXPORT std::string platformDelimeter();

static uint64_t currentDateSecs() {
    using namespace std::chrono;
    uint64_t secs = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    return secs;
}

static uint64_t currentDateMs() {
    using namespace std::chrono;
    uint64_t ms = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    return ms;
}

EXTRACHAIN_EXPORT QString extrachainVersion();
EXTRACHAIN_EXPORT std::string sodiumVersion();
EXTRACHAIN_EXPORT QString boostVersion();
EXTRACHAIN_EXPORT QString boostAsioVersion();

enum PrintDebug {
    Off = 0,
    On = 1
};

enum class HashEncode {
    Base64,
    Hex,
};

#ifdef Q_OS_WIN
static QString filePrefix = "file:///";
#else
static QString filePrefix = "file://";
#endif

EXTRACHAIN_EXPORT QString dataDir(const QString &newDir = "");
EXTRACHAIN_EXPORT qint64 diskFreeMemory();
EXTRACHAIN_EXPORT qint64 diskTotalMemory();

QByteArray intToByteArray(const int &number, const int &size);
std::string intToStdString(const int &number, const int &size);
int qByteArrayToInt(const QByteArray &number);

EXTRACHAIN_EXPORT std::string calcHash(const std::string &data, HashEncode encode = HashEncode::Base64);
EXTRACHAIN_EXPORT QByteArray calcHash(const QByteArray &data, HashEncode encode = HashEncode::Base64);
EXTRACHAIN_EXPORT std::string calcHashForFile(const std::filesystem::path &fileName,
                                              HashEncode encode = HashEncode::Base64);

std::string byteToHexString(std::vector<unsigned char> &data);
std::string byteToHexString(const std::string &data);
std::string hexStringToByte(const std::string &data);
QByteArray bytesEncode(const QByteArray &data, HashEncode encode = HashEncode::Base64);
QByteArray bytesDecode(const QByteArray &data, HashEncode encode = HashEncode::Base64);

EXTRACHAIN_EXPORT bool encryptFile(const QString &originalName, const QString &encryptName,
                                   const QByteArray &key, int blockSize = 60007);
EXTRACHAIN_EXPORT bool decryptFile(const QString &encryptName, const QString &decryptName,
                                   const QByteArray &key, int blockSize = 60007);
EXTRACHAIN_EXPORT QByteArray decryptFileIntoByteArray(const QString &encryptName, const QByteArray &key,
                                                      int blockSize = 60007);
QString fileMimeType(const QString &filePath);
QString fileMimeSuffix(const QString &filePath);

std::vector<std::string> split(const std::string &s, char c);

int compare(const QByteArray &one, const QByteArray &two);

/**
 * @brief Remove data and cache files
 */
EXTRACHAIN_EXPORT void wipeDataFiles();

EXTRACHAIN_EXPORT QString detectCompiler();
EXTRACHAIN_EXPORT QNetworkAddressEntry findLocalIp(PrintDebug debug = PrintDebug::Off);
EXTRACHAIN_EXPORT QString fixFileName(const QString &fileName, const QString &replaceSymbol = "_");
EXTRACHAIN_EXPORT bool isValidIp(const QString &ip);
EXTRACHAIN_EXPORT void benchmark(std::function<void(void)> func, int count = 1000);
} // namespace Utils

namespace DataStorage {
// Main blockchain folder
static const QString BLOCKCHAIN = "blockchain";

// Temporary folder
static const QString TMP_FOLDER = "tmp";
static const QString TMP_GENESIS_BLOCK = "tmp/genesis_block";

// Folder with blocks
static const QString BLOCKCHAIN_INDEX = "blockchain/index";
static const QString ACTOR_INDEX_FOLDER_NAME = "actors";
static const QString BLOCK_INDEX_FOLDER_NAME = "blocks";

// Dfs
static const int DATA_OFFSET = 512;

enum typeDataRow {
    UNIVERSAL,
    STAKING
};
} // namespace DataStorage

namespace KeyStore {
// To store user private/public keys
static const std::string folder = "keystore";
static const std::string format = ".profile";
static const std::string profiles = "profiles";

// TODO: remove
static const QString KEY_TYPE = ".key";
QString makeKeyFileName(QString name);
}

namespace SearchEnum {
enum class BlockParam {
    Id = 0,
    Approver,
    Data,
    Hash,
    Null
};

enum class TxParam {
    UserSender = 0,
    UserReceiver,
    UserApprover,
    UserSenderOrReceiver,
    UserSenderOrReceiverOrToken,
    User, // sender or receiver or approver
    Hash,
    Null
};

[[maybe_unused]] static QString toString(BlockParam param) {
    switch (param) {
    case BlockParam::Id:
        return "Id";
    case BlockParam::Approver:
        return "Approver";
    case BlockParam::Data:
        return "Data";
    case BlockParam::Hash:
        return "Hash";
    default:
        return QString();
    }
}

[[maybe_unused]] static BlockParam fromStringBlockParam(QByteArray s) {
    if (s == "Id")
        return BlockParam::Id;
    if (s == "Approver")
        return BlockParam::Approver;
    if (s == "Data")
        return BlockParam::Data;
    if (s == "Hash")
        return BlockParam::Hash;
    return BlockParam::Null;
}

[[maybe_unused]] static QString toString(TxParam param) {
    switch (param) {
    case TxParam::UserSender:
        return "UserSender";
    case TxParam::UserReceiver:
        return "UserReceiver";
    case TxParam::UserApprover:
        return "UserApprover";
    case TxParam::UserSenderOrReceiver:
        return "UserSenderOrReceiver";
    case TxParam::User:
        return "User";
    case TxParam::Hash:
        return "Hash";
    default:
        return QString();
    }
}

[[maybe_unused]] static TxParam fromStringTxParam(QByteArray s) {
    if (s == "User")
        return TxParam::User;
    if (s == "UserApprover")
        return TxParam::UserApprover;
    if (s == "UserReceiver")
        return TxParam::UserReceiver;
    if (s == "UserSender")
        return TxParam::UserSender;
    if (s == "UserSenderOrReceiver")
        return TxParam::UserSenderOrReceiver;
    if (s == "Hash")
        return TxParam::Hash;
    return TxParam::Null;
}
} // namespace SearchEnum

// namespace FileSystem {
// inline static QString pathConcat(const QString &pl, const QString &pr) {
//    return QDir::cleanPath(pl + "/" + pr);
//};
// QString createSubDirectory(const QString &parentDirStr, const QString &subDirStr);
// QList<std::tuple<QString, QString>> listFiles(const QString &dirPath,
//                                              const QStringList &ignoreList = QStringList());
//} // namespace FileSystem

struct EXTRACHAIN_EXPORT Notification {
    enum NotifyType {
        TxToUser,
        TxToMe,
        ChatMsg,
        ChatInvite,
        NewPost,
        NewEvent,
        NewFollower
    };
    long long time;
    NotifyType type;
    QByteArray data = "";
};

QDebug operator<<(QDebug d, const Notification &n);

#define TIMER_START(name) \
    QElapsedTimer name;   \
    name.start();
#define TIMER_END(name) qDebug() << name.elapsed() << "ms for timer" << #name;

// namespace Tools {
// template <typename T>
// std::vector<unsigned char> typeToByteArray(T integerValue) {
//    std::vector<unsigned char> res;
//    unsigned char *b = (unsigned char *)(&integerValue);
//    unsigned char *e = b + sizeof(T);
//    std::copy(b, e, back_inserter(res));
//    return res;
//}

// template <typename T>
// T byteArrayToType(std::vector<unsigned char> value) {
//    T *res;
//    res = reinterpret_cast<T *>(value.data());
//    return *res;
//}

// template <typename T>
// std::string typeToStdStringBytes(T integerValue) {
//    std::string res;
//    unsigned char *b = (unsigned char *)(&integerValue);
//    unsigned char *e = b + sizeof(T);
//    std::copy(b, e, back_inserter(res));
//    return res;
//}

// template <typename T>
// T stdStringBytesToType(std::string value) {
//    T *res;
//    res = reinterpret_cast<T *>(value.data());
//    return *res;
//}
//}

#endif // UTILS_H
