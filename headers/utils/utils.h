#ifndef UTILS_H
#define UTILS_H

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include "utils/bignumber.h"
#include "utils/Keccak256.h"
#include "network/socket_pair.h"
#include <QStringList>
#include <string>
#include <sstream>
#include <QDateTime>
#include <QStorageInfo>
#include <QCoreApplication>
#include <exception>

namespace Network {
static QString serverIp = "51.68.181.53";
static const int build = 1500;
static const unsigned long FRAGMENT_STACK_SIZE = 2048;
static const int DFS_FILE_STATUS_CHECK_TIME = 1000;
struct DataStruct
{
    QByteArray msg;
    SocketPair receiver;
};
}

namespace TMP {
static QByteArray *companyActorId = new QByteArray("0");
};

struct indexRow
{
    indexRow(std::string _hash, long long pos, short use);
    std::string hash = "";
    long long currentPosition;
    bool used;
};
class FileList
{

public:
    FileList();
    ~FileList();
    void add(QByteArray hash, QByteArray data);
    void remove(QByteArray element);
    QByteArray operator[](int value);
    QByteArray at(QByteArray hash);
    QByteArray at(int value);
    int getIndexSize();
    QByteArray getHash(int value);

    void setFileList(const QFile &value);

private:
    QList<indexRow>::iterator find(QByteArray key);
    QList<indexRow> indexList;
    QFile fileList;

    void init();
    void checkForDelete();
    bool check(QByteArray hash); // IF HASH HAVE -> END
    const QByteArray DATA_EMPTY = "null";
    const int FIELD_SIZE = 4;
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
}
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
}

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
                                               "sign    TEXT  NOT NULL, "
                                               "PRIMARY KEY (id, type)"
                                               ");";
    static const std::string cardTableCreation =
        "CREATE TABLE IF NOT EXISTS " + cardTableName + cardTableFields;
    static const std::string cardDeletedTableCreation =
        "CREATE TABLE IF NOT EXISTS " + cardDeletedTableName + cardTableFields;

    static const std::string chatIdTableName = "ChatId";
    static const std::string chatIdStorage = "CREATE TABLE IF NOT EXISTS " + chatIdTableName
        + " ("
          "chatId  TEXT  PRIMARY KEY NOT NULL, "
          "key     TEXT              NOT NULL, "
          "owner   TEXT              NOT NULL );";

    static const std::string chatUserTableName = "Users";
    static const std::string chatUserStorage = "CREATE TABLE IF NOT EXISTS " + chatUserTableName
        + " ("
          "userId  TEXT  PRIMARY KEY NOT NULL);";

    static const std::string chatMessageTableName = "Chat";
    static const std::string sessionChatMessageStorage = "CREATE TABLE IF NOT EXISTS " + chatMessageTableName
        + " ("
          "messId   TEXT PRIMARY KEY  NOT NULL, "
          "userId   TEXT              NOT NULL, "
          "message  BLOB              NOT NULL, "
          "type     TEXT              NOT NULL, "
          "session  TEXT              NOT NULL, "
          "date     TEXT              NOT NULL );";

    static const std::string storedTableName = "Stored";
    static const std::string storedTableCreation = "CREATE TABLE IF NOT EXISTS " + storedTableName
        + " ("
          "hash  TEXT PRIMARY KEY NOT NULL, "
          "data  BLOB             NOT NULL, "
          "range TEXT             NOT NULL, "
          "type  INT              NOT NULL, "
          "uid   TEXT             NOT NULL, "
          "sign  BLOB             NOT NULL, "
          "prevHash TEXT          NOT NULL);";

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
          "chatId  TEXT PRIMARY KEY NOT NULL, "
          "message BLOB             NOT NULL, "
          "owner   TEXT             NOT NULL  );";

    static const std::string notificationTable = "Notification";
    static const std::string notificationTableCreation = "CREATE TABLE IF NOT EXISTS " + notificationTable
        + " ("
          "time  INTEGER  PRIMARY KEY NOT NULL, "
          "type  INT                  NOT NULL, "
          "data  TEXT                 NOT NULL  );";

    static const std::string propertiesTableName = "Properties";
    static const std::string textTableName = "Text";
    static const std::string attachTableName = "Attachments";
    static const std::string commentsTableName = "Comments";
    static const std::string likesTableName = "Likes";
    static const std::string savedPostsTableName = "Posts";
    static const std::string savedEventsTableName = "Events";
    static const std::string usersFollowersTableName = "UsersFollowers";
    static const std::string usersConfirmedTableName = "UsersConfirmed";
    static const std::string usersMarkedTableName = "UsersMarked";

    static const std::string propertiesFields = " ("
                                                "version    TEXT    NOT NULL,"
                                                "sender     TEXT    NOT NULL,"
                                                "dateCreate INTEGER NOT NULL,"
                                                "dateModify INTEGER NOT NULL,"
                                                "latitude   NUMERIC NOT NULL,"
                                                "longitude  NUMERIC NOT NULL,";

    static const std::string postPropertiesTableCreation = "CREATE TABLE IF NOT EXISTS " + propertiesTableName
        + propertiesFields
        + "sign         TEXT     NOT NULL"
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
        + "sign         TEXT    NOT NULL"
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
          "commentId  INTEGER PRIMARY KEY AUTOINCREMENT, "
          "sender     TEXT                     NOT NULL, "
          "message    BLOB                     NOT NULL, "
          "date       TEXT                     NOT NULL, "
          "sign       TEXT                     NOT NULL  "
          ");";

    static const std::string likesTableCreation = "CREATE TABLE IF NOT EXISTS " + likesTableName
        + " ("
          "liker TEXT PRIMARY KEY NOT NULL,"
          "sign  TEXT            NOT NULL);";

    static const std::string savedPostsTableCreation = "CREATE TABLE IF NOT EXISTS " + savedPostsTableName
        + " ("
          "user TEXT NOT NULL,"
          "post TEXT NOT NULL,"
          "PRIMARY KEY (user, post)"
          ");";
    static const std::string savedEventsTableCreation = "CREATE TABLE IF NOT EXISTS " + savedEventsTableName
        + " ("
          "user TEXT NOT NULL,"
          "event TEXT NOT NULL,"
          "PRIMARY KEY (user, event)"
          ");";

    static const std::string usersFollowersTableCreation = "CREATE TABLE IF NOT EXISTS "
        + usersFollowersTableName
        + " ("
          "userId TEXT PRIMARY KEY NOT NULL,"
          "sign  TEXT            NOT NULL);";
    static const std::string usersConfirmedTableCreation = "CREATE TABLE IF NOT EXISTS "
        + usersConfirmedTableName
        + " ("
          "userId TEXT PRIMARY KEY NOT NULL,"
          "sign  TEXT            NOT NULL);";
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
          "digSig       TEXT  NOT NULL "
          ");";
    static const std::string SignTable = "Signatures";
    static const std::string SignBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + SignTable
        + " ("
          "actorId      TEXT  NOT NULL, "
          "digSig       TEXT  NOT NULL  "
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
    // How many files one section folder will store
    static const int SECTION_SIZE = 1000;

    // How often to construct block from pending transactions (in miliseconds)
    static const int BLOCK_CREATION_PERIOD = 1000;

    // How often to construct genesis block (in blocks)
    static const int CONSTRUCT_GENESIS_EVERY_BLOCKS = 1000;

    // Max number of saved blocks in mem index
    static const int MEM_INDEX_SIZE_LIMIT = 1000;
} // namespace DataStorage

namespace Net {

    // Type of Protocol. Should be changed according to client in use.
    static const QByteArray PROTOCOL_VERSION = "ExtraCoin_v4";

    // Default gas for transaction
    static const int DEFAULT_GAS = 10;

    // Networking will work only if there are enough peers
    static const int MINIMUM_PEERS = 1;

    // Get Message is considered successful only after NECESSARY_RESPONSE_COUNT
    // responses
    static const int NECESSARY_RESPONSE_COUNT = 1; // 3

    enum TypeSend
    {
        ALL,
        EXCEPT,
        FOCUSED
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
static const int DFS_FIELD_SIZE = 8;
static const int DEFAULT_FIELD_SIZE = 8;

static const QByteArray DEFAULT_FIELD_SPLITTER = ":";
static const QByteArray ACTOR_FIELD_SPLITTER = ":";
static const QByteArray BLOCK_FIELD_SPLITTER = ";";
static const QByteArray USER_FIELD_SPLITER = "~";

static const QByteArray TX_FIELD_SPLITTER = "|";
static const QByteArray TX_PAIR_FIELD_SPLITTER = "--";
static const QByteArray GENESIS_ROW_FIELD_SPLITTER = "->";

static const QByteArray DEFAULT_LIST_SPLITTER = ",";

static const QByteArray NET_MESSAGE_HEADER_FIELD_SPLITTER = "##";
static const QByteArray NET_MESSAGE_FIELD_SPLITER = "&";

static const QByteArray INFORMATION_SEPARATOR_ONE = "\u0001E\u0001F\u0001C\u0001E";
static const QByteArray INFORMATION_SEPARATOR_TWO = "\u0001C\u0001F\u0001E\u0001C"; // used in network
static const QByteArray INFORMATION_SEPARATOR_THREE = "\u0001E\u0001F\u000C\u0001F";

static const QByteArray Coin_Price_Delimiter_2 = "coin_delimetr";
static const QByteArray Coin_Price_Delimiter = "coin_price";

static const QByteArray DFS_STORED_DELIMETR = "--";
static const QByteArray DFS_HEADER_END_DELIMETR = "$";
static const QByteArray DFS_DFSTRUCT_DELIMETR = "**";
static const QByteArray DFS_ROOT_CARD_FILE_DELIMITER = "->";
static const QByteArray DFS_ROOT_CARD_FILE_SECTION_DELIMITER = "##";
static const QByteArray DFS_CARD_FILE_UNIVERSAL_DELIMITER = "=";
static const QByteArray DFS_CARD_FILE_SECTION_DELIMETR = "|";

QByteArray serialize(const QList<QByteArray> &list);
// QByteArray serialize(const QList<QString> &list);
QByteArray serialize(const QList<QByteArray> &list, const QByteArray &delimiter);
// QByteArray serialize(const QList<QString> &list, const QByteArray
// &delimiter);
QByteArray serialize(const QList<QByteArray> &list, char delimiter);
QList<QByteArray> deserialize(const QByteArray data, const QByteArray &delim);
QString serializeString(const QStringList &list);
QString serializeString(const QStringList &list, const QByteArray &delimiter);
QStringList deserializeString(const QString &serialized);
QList<QString> deserialize(const QString &serialized, char delimiter);
QByteArray serializeStored(const QList<QByteArray> list);
QList<QByteArray> desirializeStored(const QByteArray &serialize);
QByteArray universalSerialize(const QList<QByteArray> &list, const int &fiels_size = DEFAULT_FIELD_SIZE);
QList<QByteArray> universalDeserialize(const QByteArray &serialized,
                                       const int &fiels_size = DEFAULT_FIELD_SIZE);
} // namespace Serialization

namespace Utils {
// QByteArray encodeHex(const QByteArray &dec);
// QByteArray encodeHex(byte *dec);
// QByteArray decodeHex(const QByteArray &hex);

qint64 checkMemoryFree();  // MB
qint64 checkMemoryTotal(); // MB

QByteArray intToByteArray(const int &number, const int &size);
int qByteArrayToInt(const QByteArray &number);

QByteArray calcKeccak(const QByteArray &data);
QByteArray calcKeccakForFile(const QString &path);

std::vector<std::string> split(const std::string &s, char c);
std::vector<std::string> split(const std::string &s);

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
namespace ChatStorage {
// keystore/chats/[chat ID]/[sessionID]/ users,key etc.
static const QByteArray STORED_CHATS = "data/";
// static const QByteArray SESSIONS = "/sessions/";
}
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
    UNIVERSAL
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
}
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

struct notification
{
    enum NotifyType
    {
        TxToUser,
        TxToMe,
        ChatMsg,
        ChatInvite,
        NewPost,
        NewFollower
    };
    long long time;
    NotifyType type;
    QByteArray data = "";
};

#endif // UTILS_H
