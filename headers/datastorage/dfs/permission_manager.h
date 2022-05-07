#pragma once

#include <QObject>

#include "datastorage/actor.h"
#include "dfs_controller.h"
#include "utils/db_connector.h"

class EXTRACHAIN_EXPORT PermissionManager : public QObject {
    Q_OBJECT
public:
    enum CriticalErrors {
        RootDirCreateError = -2,
        ActorDirCreateError = -3,

        DBOpenError = -4,
        DBCreateTableError = -5,
        DBPermissionEntryDuplicate = -6,
        NotSupportedPermission
    };

    enum Permission {
        Read = 0,
        Write = 1,  // Add new entry
        Delete = 2, // Delete any entry
        Edit = 3,   // Write and Delete permissions

        NoPermission = 4
    };

    QStringList permissions { "Read", "Write", "Delete", "Edit", "NoPermission" };

    struct GetPermissionMsg;
    struct SetPermissionMsg;

public:
    PermissionManager(QObject *parent = nullptr);
    ~PermissionManager();

    bool initPermissionDB(const Actor<KeyPrivate> &actor);

    Permission getPermission(const Actor<KeyPrivate> &actor, const GetPermissionMsg &msg);
    bool setPermission(const Actor<KeyPrivate> &actor, const SetPermissionMsg &msg);

private:
    //    QString createDirectory(const Actor<KeyPrivate> &actor);
    //    QString makeActorDirPath(const Actor<KeyPrivate> & actor);
    //    QString makeServiceDirPath(const Actor<KeyPrivate> & actor);

    DBRow makeDBRow(const QString &fileHash, const Permission permission, const QString &userId,
                    const QString &signature);
    //    DBRow findDBRow(const QString & userId, const QString & fileHash);

    Permission getUserPermission(const QString &userId, const QString &fileHash);
    Permission getHighestPermission(const QString &userId, const QString &fileHash);

public:
    static constexpr int32_t sharedId = -1;

private:
    static const QString PermissionFileName;
    static const QString ServiceDir;
    static const QString RootDir;

private:
    // DBConnector m_db;
};

struct PermissionManager::GetPermissionMsg {
    std::string userId;
    std::string fileHash;

    MSGPACK_DEFINE(userId, fileHash)
};

struct PermissionManager::SetPermissionMsg {
    std::string userId;
    std::string fileHash;
    std::string permission;

    MSGPACK_DEFINE(userId, fileHash, permission)
};
