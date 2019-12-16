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
namespace Network {
static const unsigned long FRAGMENT_STACK_SIZE = 1024;
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
    static const std::string userCardTable = "CREATE TABLE IF NOT EXISTS UserCards ("
                                             "uid  TEXT PRIMARY KEY NOT NULL, "
                                             "path TEXT             NOT NULL );";
    static const std::string cardTableName = "Items";
    static const std::string cardTable = "CREATE TABLE IF NOT EXISTS " + cardTableName
        + " ("
          "path    TEXT PRIMARY KEY NOT NULL, "
          "date    INT              NOT NULL, "
          "type    INT              NOT NULL, "
          "subtype INT                      , "
          "hash    TEXT             NOT NULL);";
    static const std::string lsTableName = "Counters";
    static const std::string lastSectionTable = "CREATE TABLE IF NOT EXISTS " + lsTableName
        + " ("
          "type    INT  PRIMARY KEY NOT NULL, "
          "counter TEXT             NOT NULL );";

    static const std::string subscribeFollowerColumn = "Subscribers";
    static const std::string tableFollower = "CREATE TABLE IF NOT EXISTS " + subscribeFollowerColumn
        + " ("
          "subscriber    TEXT PRIMARY KEY NOT NULL,"
          "sign TEXT             NOT NULL)";
    static const std::string subscribeColumn = "Subscriptions";
    static const std::string tableMySubscribe = "CREATE TABLE IF NOT EXISTS " + subscribeColumn
        + " ("
          "subscription    TEXT PRIMARY KEY NOT NULL);";

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
    static const QString PROTOCOL_VERSION = "ExtraCoin_v1";

    // Default gas for transaction
    static const int DEFAULT_GAS = 10;

    // Networking will work only if there are enough peers
    static const int MINIMUM_PEERS = 1;

    // Get Message is considered successful only after NECESSARY_RESPONSE_COUNT
    // responses
    static const int NECESSARY_RESPONSE_COUNT = 1; // 3
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
static const int DFS_FIELD_SIZE = 8;
static const int DEFAULT_FIELD_SIZE = 4;

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
} // namespace Utils
namespace ChatStorage {
// keystore/chats/[chat ID]/[sessionID]/ users,key etc.
static const QByteArray STORED_CHATS = "keystore/chats/";
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

#endif // UTILS_H
