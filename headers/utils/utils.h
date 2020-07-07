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

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCborStreamWriter>
#include <QCborStreamReader>
#include <QList>
#include <QSettings>
#include <QStorageInfo>
#include <QString>
#include <QStringList>
#include <exception>
#include <sstream>
#include <string>

#include "dfs/types/headers/dfstruct.h"
#include "network/socket_pair.h"
#include "utils/Keccak256.h"
#include "utils/bignumber.h"
#include "enc/algorithms/blowfish_crypt.h"

namespace Network {
static QString serverIp = "51.68.181.52";
static const int build = 1500;
static const unsigned long FRAGMENT_STACK_SIZE = 2048;
static const int DFS_FILE_STATUS_CHECK_TIME = 1000;
struct DataStruct
{
    QByteArray msg;
    SocketPair receiver;
};
} // namespace Network

namespace TMP {
static QByteArray *companyActorId = new QByteArray("0");
};

namespace net {
static QByteArray readNetManagerIdentificator()
{
    QFile file(".settings");
    file.open(QIODevice::ReadOnly);
    QByteArray id = file.readAll();
    file.close();
    return id;
}
static QByteArray dfsreadNetManagerIdentificator()
{
    QFile file(".dsettings");
    file.open(QIODevice::ReadOnly);
    QByteArray id = file.readAll();
    file.close();
    return id;
}
} // namespace net
class Transaction;
// using namespace CryptoPP;
namespace storedSpace {

enum State
{
    NEWSTATE,
    DELSTATE,
    CHANGEDS,
    UNRECOGS
};
QByteArray toByteArray(State state);
QString toString(State state);
State convertToDFSstate(QByteArray state);
} // namespace storedSpace

namespace Resolver {
enum Lifetime
{
    SHORT = 0,
    LONG = 1
};
enum Type
{
    BLOCKCHAIN = 0,
    ACTORS = 1,
    DFS = 2,
    GENERAL = 3
};
} // namespace Resolver

namespace Config {

// Message pattern for qDebug (see
// http://doc.qt.io/qt-5/qtglobal.html#qSetMessagePattern)
const QString MESSAGE_PATTERN = "[%{time h:mm:ss.zzz}][%{function}][%{type}]: %{message}";

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
          "messId   BLOB PRIMARY KEY NOT NULL, "
          "userId   BLOB             NOT NULL, "
          "message  BLOB             NOT NULL, "
          "type     BLOB             NOT NULL, "
          "session  BLOB             NOT NULL, "
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

    // Type of Protocol. Should be changed according to client in use.
    static const QByteArray PROTOCOL_VERSION = "ExtraChain_v5";

    // Default gas for transaction
    static const int DEFAULT_GAS = 10;

    // Networking will work only if there are enough peers
    static const int MINIMUM_PEERS = 1;

    // Get Message is considered successful only after NECESSARY_RESPONSE_COUNT
    // responses
    static const int NECESSARY_RESPONSE_COUNT = 1; // 3

    enum TypeSend
    {
        All,
        Except,
        Focused,
        Default
    };
} // namespace Net
} // namespace Config

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

QByteArray serialize(const QList<QByteArray> &list, const int &fiels_size = DEFAULT_FIELD_SIZE);
QList<QByteArray> deserialize(const QByteArray &serialized, const int &fiels_size = DEFAULT_FIELD_SIZE);
QByteArray serializeMap(const QMap<QString, QByteArray> &map);
QMap<QString, QByteArray> deserializeMap(const QByteArray &data);

QByteArray fromMap(const QMap<QString, QByteArray> &map);
QByteArray fromList(const QByteArrayList &list);
QByteArrayList toList(const QByteArray &data);
QMap<QString, QByteArray> toMap(const QByteArray &data);
int length(const QByteArray &data);
} // namespace Serialization

namespace Utils {
// QByteArray encodeHex(const QByteArray &dec);
// QByteArray encodeHex(byte *dec);
// QByteArray decodeHex(const QByteArray &hex);

#ifdef Q_OS_WIN
static QString filePrefix = "file:///";
#else
static QString filePrefix = "file://";
#endif

QString dataName();
qint64 checkMemoryFree();  // MB
qint64 checkMemoryTotal(); // MB

QByteArray intToByteArray(const int &number, const int &size);
int qByteArrayToInt(const QByteArray &number);

QByteArray calcKeccak(const QByteArray &data);
QByteArray calcKeccakForFile(const QString &path);
bool encryptFile(const QString &originalName, const QString &encryptName, const QByteArray &key,
                 int blockSize = 60007);
bool decryptFile(const QString &encryptName, const QString &decryptName, const QByteArray &key,
                 int blockSize = 60007);
QString fileMimeType(const QString &filePath);

std::vector<std::string> split(const std::string &s, char c);

int compare(const QByteArray &one, const QByteArray &two);

/**
 * @brief Get param from message using JsonDocument
 * @param field
 * @param jsonDocuments
 * @return value
 */
QByteArray getParam(const QString &param, const QByteArray &jsonDocument);
void wipeDataFiles();
void softWipe(const QString &currentId);
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

enum typeDataRow
{
    UNIVERSAL,
    STAKING
};
} // namespace DataStorage

namespace KeyStore {
static const QString KEYSTORE = "keystore";
// To store user private/public keys
static const QString USER_KEYSTORE = "keystore/personal/";
static const QString user_actor_state = "keystore/personal/file.dat";
static const QString KEY_TYPE = ".key";
static const QString KEY_FILTER = "*.key";

QString makeKeyFileName(QString name);
} // namespace KeyStore
namespace SmartContractStorage {
static const QString CONTRACTSTORE = "keystore/contracts/";
static const QString CONTRACTPROFILE = "keystore/contracts/profile/";
} // namespace SmartContractStorage
namespace FileSystem {
void createFolderIfNotExist(QString path);
/**
 * @brief Attempts to open file
 * @param file
 * @param mode
 * @return true if file is opened successfully
 */
bool tryToOpen(QFile &file, QIODevice::OpenMode mode);
} // namespace FileSystem

namespace SearchEnum {
enum class BlockParam
{
    Id = 0,
    Approver,
    Data,
    Hash,
    Null
};

enum class TxParam
{
    UserSender = 0,
    UserReceiver,
    UserApprover,
    UserSenderOrReceiver,
    UserSenderOrReceiverOrToken,
    User, // sender or receiver or approver
    Hash,
    Null
};

static QString toString(BlockParam param)
{
    switch (param)
    {
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

static BlockParam fromStringBlockParam(QByteArray s)
{
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

static QString toString(TxParam param)
{
    switch (param)
    {
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

static TxParam fromStringTxParam(QByteArray s)
{
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

struct Notification
{
    enum NotifyType
    {
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

#endif // UTILS_H
