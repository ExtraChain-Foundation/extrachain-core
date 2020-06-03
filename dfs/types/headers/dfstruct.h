#ifndef DFSTRUCT_H
#define DFSTRUCT_H

#include <QDir>
#include <QDebug>
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <unordered_map>
#include <tuple>
#include "utils/bignumber.h"

namespace PathStruct {
static const short rFolder = 0;
static const short aId = 1;
static const short section = 2;
static const short name = 3;
}

namespace DfsStruct {
static const QString ROOT_FOOLDER_NAME = "edfs";
static const int ROOT_FOOLDER_NAME_SIZE = ROOT_FOOLDER_NAME.length();
static const int ROOT_FOOLDER_NAME_MID = ROOT_FOOLDER_NAME.length() + 1;
static const std::string ROOT_FOOLDER_NAME_STD = ROOT_FOOLDER_NAME.toStdString();
static const QString STORED_EXT = ".stored";
static const int STORED_EXT_SIZE = STORED_EXT.length();
static const QString FILE_IDENTIFICATOR = ".tmp";
static const QString ACTOR_CARD_FILE = "root";
static const QString ACTOR_CARD_LAST = "root.last";
static const QString ACTOR_CARD_FUTURE = ".future";

enum Type
{
    Unknown = 0,
    Video = 1,
    Event = 2,
    Chat = 4,
    Post = 5,
    Service = 6,
    Files = 7,
    Image = 8,
    Contract = 9,
    Private = 10,
    Error = 100,
    Stored = 200
};

Type toDfsType(QByteArray);
QByteArray toByteArray(Type);
QString toString(Type);

enum class DfsSave
{
    File,
    Static,
    StaticNonStored,
    Network
};

enum ChangeType
{
    Delete,
    Insert,
    Update,
    Bytes,
    NewColumn,
    RemoveColumn,
    CreateTable,
    RenameTable,
    DropTable,
    Global
};

enum DfsVersionType
{
    PostVersion = 0,
    PostLikesVersion = 1,
    PostCommentsVersion = 2,
    EventVersion = 3,
    EventLikesVersion = 4,
    EventCommentsVersion = 5,
    EventUsersVersion = 6,
    PrivateLikes = 7,
    PrivateSaved = 8,
    PrivateChats = 9,
    PrivateNotifications = 10,
    PrivateEvents = 11,
    PrivateChatInvite = 12,
    ServiceSubscribe = 13,
    ServiceFollower = 14,
    ServiceEvents = 15,
    ServiceChatInvite = 16,
    ChatMsg = 17,
    ChatUsers = 18
};

static QMap<DfsVersionType, int> dfsVersions = {
    { PostVersion, 1 },       { PostLikesVersion, 1 },     { PostCommentsVersion, 1 },
    { EventVersion, 1 },      { EventLikesVersion, 1 },    { EventCommentsVersion, 1 },
    { EventUsersVersion, 1 }, { PrivateLikes, 1 },         { PrivateSaved, 1 },
    { PrivateChats, 1 },      { PrivateNotifications, 1 }, { PrivateEvents, 1 },
    { PrivateChatInvite, 1 }, { ServiceSubscribe, 1 },     { ServiceFollower, 1 },
    { ServiceEvents, 1 },     { ServiceChatInvite, 1 },    { ChatMsg, 1 },
    { ChatUsers, 1 }
};

}
namespace DFS_ERRORS {

// static const int POST_CARD_FILE_NAME_NOT_COMPLETE = 501;
static const int POST_CARD_FILE_NAME_MISSIMG = 502;

// static const int CHAT_CARD_FILE_NAME_NOT_COMPLETE = 503;
static const int CHAT_CARD_FILE_NAME_MISSIMG = 504;

// static const int IMAGE_CARD_FILE_NAME_NOT_COMPLETE = 505;
static const int IMAGE_CARD_FILE_NAME_MISSIMG = 506;

// static const int EVENT_CARD_FILE_NAME_NOT_COMPLETE = 507;
static const int EVENT_CARD_FILE_NAME_MISSIMG = 508;

// static const int VIDEO_CARD_FILE_NAME_NOT_COMPLETE = 509;
static const int VIDEO_CARD_FILE_NAME_MISSIMG = 510;

// static const int SERVICE_CARD_FILE_NAME_NOT_COMPLETE = 511;
static const int SERVICE_CARD_FILE_NAME_MISSING = 512;

// static const int SYSTEM_CARD_FILE_NOT_COMPLETE = 513;
static const int SYSTEM_CARD_FILE_MISSING = 514;

// static const int CONTRACT_CARD_FILE_NOT_COMPLETE = 515;
static const int CONTRACT_CARD_FILE_MISSING = 516;

static QMap<QString, QMap<DfsStruct::Type, QString>> allDfsCardFileConnections = {};
}
namespace DFS_REQUESTS {
static const int DFS_ALL = 600;
static const int GET_USER_ID = 601;
static const int GET_MY_PRIVATE_KEY = 602;
static const int GET_USER_PUBLIC_KEY = 603;
static const int FILE_REQUEST = 604;

// server request
static const int CARD_FILE_REQUEST = 5400;
static const int IMAGE_FILE_REQUEST = 5401;
static const int POST_FILE_REQUEST = 5402;
static const int EVENT_FILE_REQUEST = 5403;
static const int PROFILE_FILE_REQUEST = 5404;
// request delimetrs
static const QByteArray REQUESTS_DATA_DELIMETRS = "^";
}

#endif // DFSTRUCT_H
