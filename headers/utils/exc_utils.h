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

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

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

    // How many files one section folder will store
    static const int SECTION_SIZE = 1000;

    // How often to construct block from pending transactions (in miliseconds)
    static const int BLOCK_CREATION_PERIOD = 2000;

    // How often to construct genesis block (in blocks)
    static const int CONSTRUCT_GENESIS_EVERY_BLOCKS = 100;

    // Max number of saved blocks in mem index
    static const int MEM_INDEX_SIZE_LIMIT = 1000;

    // How often to prove pransactions
    static const int PROVE_TXS_INTERVAL = 2000;
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

namespace ExtraCoin {
    static const uint64_t totalSupply = 300000000;
} // namespace ExtraCoin
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

EXTRACHAIN_EXPORT std::string serialize(const std::vector<std::string> &list);
EXTRACHAIN_EXPORT std::vector<std::string> deserialize(const std::string &serialized);

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
    } catch (std::exception &e) {
        qDebug() << e.what();
    }

    auto qt_bytes = QByteArray::fromStdString(data.data());
    qDebug() << "[MessagePack] Incorrect deserialize for" << qt_bytes.toBase64() << qt_bytes;
    qFatal("[MessagePack] Incorrect deserialize");
    return T();
}

template <class T>
std::vector<std::string> serializeContainer(std::vector<T> &list) {
    std::vector<std::string> result;
    for (const auto &item : list) {
        result.push_back(serialize(item));
    }
    return result;
}

template <class T>
std::vector<T> deserializeContainer(const std::vector<std::string> dataContainer) {
    std::vector<T> result;

    for (const auto &data : dataContainer) {
        const T element = deserialize<T>(data);
        result.push_back(element);
    }
    return result;
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
    Sha3_512,
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
typedef std::vector<std::string> MerkleDataBlocks;

EXTRACHAIN_EXPORT void rootMerkleHash(std::vector<std::string> &listHashes,
                                      std::vector<MerkleDataBlocks> &branchesTree, const bool isHahsing,
                                      std::string &result);
EXTRACHAIN_EXPORT std::string rootMerkleHash(std::string &data);
EXTRACHAIN_EXPORT std::vector<MerkleDataBlocks> splitListIntoPair(std::vector<std::string> &vector,
                                                                  const bool isHahsing);
EXTRACHAIN_EXPORT void hashingElements(std::vector<std::string> &vector);
EXTRACHAIN_EXPORT std::string merkleFormula(const std::string &hash1, const std::string &hash2);
EXTRACHAIN_EXPORT std::string calcHash(const std::string &data, HashEncode encode = HashEncode::Sha3_512);
EXTRACHAIN_EXPORT std::string calcHashForFile(const std::filesystem::path &fileName,
                                              HashEncode encode = HashEncode::Sha3_512);

std::string byteToHexString(std::vector<unsigned char> &data);
std::string byteToHexString(const std::string &data);
std::string hexStringToByte(const std::string &data);

std::string bytesEncodeStdString(const std::string &data, HashEncode encode = HashEncode::Base64);
std::string bytesDecodeStdString(const std::string &data, HashEncode encode = HashEncode::Base64);
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

namespace Scripts {
static const std::string folder = "scripts";
static const std::string wasmExtention = ".wasm";
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
    Data,
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

#endif // UTILS_H
