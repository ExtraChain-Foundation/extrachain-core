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

namespace based_dfs_struct {

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
static const QString FILE_INDETIFICATOR = ".tmp";
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
    NEW,
    MERGE,
    REPLACE
};
Status convertToDFSstatus(QByteArray);
QByteArray toByteArray(Status);
QString toString(Status);

enum SubType
{
    profil,
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
    images,
    ivideo,
    events,
    system,
    chates,
    postes,
    servic,
    cdoctp
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

static const std::vector<Type> typesVec = { images, ivideo, events, system, chates, postes, servic };
static const std::vector<SubType> subTypesVec = { profil, avatar, ievent, ipost, portfolio };

static std::unordered_map<Type, QString> typeCardFilesMap = {
    { images, IMAGE_CARD_FILE_NAME },  { ivideo, VIDEO_CARD_FILE_NAME }, { events, EVENT_CARD_FILE_NAME },
    { system, SYSTEM_CARD_FILE_NAME }, { chates, CHAT_CARD_FILE_NAME },  { postes, POST_CARD_FILE_NAME },
    { servic, SERVICE_CARD_FILE_NAME }
};

static std::unordered_map<Type, QString> cardFileConnections = {};

static std::unordered_map<QFile *, bool> fileStatus = {};

class DfStruct
{
private:
    Type type;
    Status status;  // status in worker
    BigNumber name; // name in etalonium system
    long long size;
    QDateTime time;  // date and time when item have been created
    QByteArray hash; // hash of object
    QByteArray path; // path in etalonium system
    SubType subType;
    QByteArray data;   // information about location of object on the device/ VALIK DOLBOEB
    BigNumber actorId; // owner user ID
public:
    DfStruct(const DfStruct &dfStruct);
    DfStruct(const QString &_file_name, based_dfs_struct::Status status);
    DfStruct(const QByteArray &serialized);
    virtual ~DfStruct()
    {
    }

public:
    Type getType() const;
    void setType(const Type &value);
    Status getStatus() const;
    void setStatus(const Status &value);
    BigNumber getName() const;
    void setName(const BigNumber &value);
    long long getSize() const;
    void setSize(long long value);
    QDateTime getTime() const;
    void setTime(const QDateTime &value);
    QByteArray getHash() const;
    void setHash(const QByteArray &value);
    QByteArray getPath() const;
    void setPath(const QByteArray &value);
    SubType getSubType() const;
    void setSubType(const SubType &value);
    QByteArray getData() const;
    void setData(const QByteArray &value);
    BigNumber getActorId() const;
    void setActorId(const BigNumber &value);

protected:
    QByteArray madeFolderDir(const Type &type) const;
    DfStruct operator=(const DfStruct &dfStruct);
    //    int makeSystemDir(const BigNumber &userId);
    bool operator==(const DfStruct &dfStruct);
    virtual const QByteArray serialize() const;
    void setFileCardFile() const;
    int readRootFolder() const;
    int makeSystemDir() const;
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

static QMap<QString, QMap<based_dfs_struct::Type, QString>> allDfsCardFileConnections = {};
}
namespace DFS_REQUESTS {
static const int DFS_ALL = 600;
static const int GET_USER_ID = 601;
static const int GET_MY_PRIVATE_KEY = 602;
static const int GET_USER_PUBLIC_KEY = 603;

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
