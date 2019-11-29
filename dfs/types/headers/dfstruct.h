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
#include "utils/utils.h"

namespace PathStruct {
//"data/actorId/section/fileName"
static const short rFolder = 0;
static const short aId = 1;
static const short section = 2;
static const short name = 3;
}

namespace dfsStruct {

static const QString ROOT_FOOLDER_NAME = "data";
static const QString USER_DATA_FOLDER = "data/user";
static const QString USER_KEYS_DIR = "data/user/key";
static const QString STORED_FILE_NAME = ".stored";
static const QString CLON_SIGN = ".clone";
static const QString MINI_IMAGES = "/mini";
// cards file ;
static const QString ROOT_CARD_FILE_NAME = "data/card_file.root";
static const QString POST_CARD_FILE_NAME = "/card_file.post";
static const QString EVENT_CARD_FILE_NAME = "/card_file.event";
static const QString CHAT_CARD_FILE_NAME = "/card_file.chat";
static const QString IMAGE_CARD_FILE_NAME = "/card_file.image";
static const QString VIDEO_CARD_FILE_NAME = "/card_file.video";
static const QString SERVICE_CARD_FILE_NAME = "/card_file.service";
static const QString SYSTEM_CARD_FILE_NAME = "/card_file.system";
// temp files
static const QString FILE_IDENTIFICATOR = ".tmp";
static const QString ACTOR_CARD_FILE = ".root";

enum State
{
    NEWSTATE,
    DELSTATE,
    CHANGEDS
};
State convertToDFSstate(QByteArray);
QByteArray toByteArray(State);
QString toString(State);

enum Status
{
    NEW = 0,
    MERGE,
    REPLACE
};
Status convertToDFSstatus(QByteArray);
QByteArray toByteArray(Status);
QString toString(Status);

enum SubType
{
    profil = 0,
    avatar,
    ievent,
    ipost,
    mini,
    portfolio
};
SubType convertToDFSSubType(QByteArray);
QByteArray toByteArray(SubType);
QString toString(SubType);

enum Type
{
    images = 0,
    video = 1,
    event = 2,
    system = 3,
    chat = 4,
    post = 5,
    service = 6,
    cdoctp = 7,
    card = 8
};
Type convertToDFType(QByteArray);
QByteArray toByteArray(Type);
QString toString(Type);

enum Key
{
    storedIndex,
    dfsIndex
};
Key convertToKey(QByteArray key);
QByteArray toByteArray(Key);
QString toString(Key);

static const std::vector<Type> typesVec = { images, video, event, system, chat, post, service };
static const std::vector<SubType> subTypesVec = { profil, avatar, ievent, ipost, portfolio };

static std::unordered_map<Type, QString> typeCardFilesMap = {
    { images, IMAGE_CARD_FILE_NAME },   { video, VIDEO_CARD_FILE_NAME }, { event, EVENT_CARD_FILE_NAME },
    { system, SYSTEM_CARD_FILE_NAME },  { chat, CHAT_CARD_FILE_NAME },   { post, POST_CARD_FILE_NAME },
    { service, SERVICE_CARD_FILE_NAME }
};

static std::unordered_map<Type, QString> cardFileConnections = {};

static std::unordered_map<QFile *, bool> fileStatus = {};

class DfStruct
{
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

static QMap<QString, QMap<dfsStruct::Type, QString>> allDfsCardFileConnections = {};
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
