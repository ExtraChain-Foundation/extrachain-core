#include "datastorage/dfs/permission_manager.h"

const QString PermissionManager::PermissionFileName = ".perm";
const QString PermissionManager::ServiceDir = "Service";
const QString PermissionManager::RootDir = "dfs";

PermissionManager::PermissionManager(QObject *parent)
    : QObject(parent) {
}

PermissionManager::~PermissionManager() {
}

bool PermissionManager::initPermissionDB(const Actor<KeyPrivate> &actor) {
    //    qDebug() << __FUNCTION__ << "DB initialization";

    //    createDirectory(actor);

    //    QString serviceDirPath = makeServiceDirPath(actor);
    //    QString permissionFilePath = FileSystem::pathConcat(serviceDirPath, PermissionFileName);

    //    const bool dbExists = QFileInfo::exists(permissionFilePath);

    //    if (!m_db.open(permissionFilePath.toStdString()))
    //    {
    //        qDebug() << __FUNCTION__ << "Can't open the permission DB";
    //        QCoreApplication::exit(DBOpenError);
    //    }

    //    // Allow read file everyone
    //    const auto & premReadSig = actor.key().sign(permissions[Permission::Read].toLatin1());

    //    // Allow edit file
    //    const auto & premEditSig = actor.key().sign(permissions[Permission::Edit].toLatin1());
    //    const QString & userId = actor.idStd().c_str();

    //    const std::vector<DBRow> rowList = {
    //        makeDBRow(PermissionFileName, Permission::Read, QString::number(sharedId), premReadSig),
    //        makeDBRow(PermissionFileName, Permission::Edit, userId, premEditSig)
    //    };

    //    if(dbExists)
    //    {
    //        qDebug() << __FUNCTION__ << " Delete legacy table";

    //        if(getHighestPermission(actor.idStd().c_str(), PermissionFileName) != Permission::Edit)
    //        {
    //            // Allow edit file
    //            if(!m_db.insert(Config::DataStorage::permissionTable, rowList[1]))
    //            {
    //                qDebug() << __FUNCTION__ << "Insert row failure";
    //                return false;
    //            }
    //        }
    //    }

    //    if(!dbExists)
    //    {
    //        qDebug() << __FUNCTION__ << "Create permission table";

    //        if(!m_db.createTable(Config::DataStorage::permissionTableCreate))
    //        {
    //            qDebug() << __FUNCTION__ << "Create table failure";
    //            QCoreApplication::exit(DBCreateTableError);
    //        }

    //        for(const auto & row: rowList)
    //        {
    //            if(!m_db.insert(Config::DataStorage::permissionTable, row))
    //            {
    //                qDebug() << __FUNCTION__ << "Insert row failure";
    //                return false;
    //            }
    //        }
    //    }
    return true;
}

PermissionManager::Permission PermissionManager::getHighestPermission(const QString &userId,
                                                                      const QString &fileHash) {
    Permission permFilePermission = getUserPermission(userId, fileHash);

    Permission sharedFilePermission = getUserPermission(QString::number(sharedId), fileHash);

    if (sharedFilePermission == Edit || permFilePermission == Edit)
        return Edit;

    if (sharedFilePermission == Delete || permFilePermission == Delete)
        return Delete;

    if (sharedFilePermission == Write || permFilePermission == Write)
        return Write;

    if (sharedFilePermission == Read || permFilePermission == Read)
        return Read;

    return NoPermission;
}

PermissionManager::Permission PermissionManager::getPermission(const Actor<KeyPrivate> &actor,
                                                               const GetPermissionMsg &msg) {
    const QString &userId = msg.userId.c_str();
    const QString &fileHash = msg.fileHash.c_str();

    Permission permFilePermission = getHighestPermission(actor.idStd().c_str(), PermissionFileName);

    if (permFilePermission == Read || permFilePermission == Write || permFilePermission == Delete
        || permFilePermission == Edit) {
        Permission targetFilePermission = getHighestPermission(userId, fileHash);

        return targetFilePermission;
    }

    qDebug() << __FUNCTION__ << " Actor " << actor.idStd().c_str()
             << " has no permission to read the Permissions file";
    return permFilePermission;
}

bool PermissionManager::setPermission(const Actor<KeyPrivate> &actor, const SetPermissionMsg &msg) {
    //    const QString &userId = msg.userId.c_str();
    //    const QString &fileHash = msg.fileHash.c_str();
    //    const QString &permissionStr = msg.permission.c_str();
    //    const Permission newPermission = static_cast<Permission>(permissions.indexOf(permissionStr));

    //    if (newPermission != Permission::Read && newPermission != Permission::Write
    //        && newPermission != Permission::Delete && newPermission != Permission::Edit
    //        && newPermission != Permission::NoPermission) {
    //        qDebug() << __FUNCTION__ << " Invalid permission requested";
    //        return false;
    //    }

    //    Permission actorPermission = getUserPermission(actor.idStd().c_str(), PermissionFileName);

    //    Permission userPermission = getPermission(actor, { userId.toStdString(), fileHash.toStdString() });

    //    if (userPermission == newPermission) {
    //        qDebug() << __FUNCTION__ << " User " << userId << " already has correct permission";
    //        return true;
    //    }

    //    DBRow currentPermissionRow = findDBRow(userId, fileHash);

    //    // Delete file from the DB
    //    if (newPermission == Permission::NoPermission) {
    //        if (!(actorPermission == Delete || actorPermission == Edit)) {
    //            qDebug() << __FUNCTION__ << " Actor " << actor.idStd().c_str()
    //                     << " has no permission to change the Permissions file";
    //            return false;
    //        }

    //        if (currentPermissionRow.empty()) {
    //            qDebug() << __FUNCTION__ << "Permission already deleted from the DB";
    //            return true;
    //        }

    //        if (!m_db.deleteRow(Config::DataStorage::permissionTable, currentPermissionRow)) {
    //            qDebug() << __FUNCTION__ << " Delete DB entry failure.";
    //            return false;
    //        }
    //    }

    //    // Update PERMISSION and SIGNATURE for the existing DB entry
    //    const std::string &newPermissionStr = permissions[newPermission].toStdString();
    //    const std::string &newSignatureStr =
    //        actor.key().sign(QByteArray::fromStdString(newPermissionStr)).toStdString();

    //    if (!(actorPermission == Write || actorPermission == Edit)) {
    //        qDebug() << __FUNCTION__ << " Actor " << actor.idStd().c_str()
    //                 << " has no permission to change the Permissions file";
    //        return false;
    //    }

    //    // Create new DB entry
    //    if (currentPermissionRow.empty()) {
    //        qDebug() << __FUNCTION__ << "Add new entry to the Permissions: User " << userId << ", file "
    //                 << fileHash;

    //        const auto &signature = actor.key().sign(QByteArray::fromStdString(newPermissionStr));

    //        DBRow row = makeDBRow(fileHash, newPermission, userId, newSignatureStr.c_str());

    //        if (!m_db.insert(Config::DataStorage::permissionTable, row)) {
    //            qDebug() << __FUNCTION__ << "Insert row failure";
    //            return false;
    //        }
    //    } else // Update existing entry
    //    {
    //        const std::string &fileHashStr = currentPermissionRow["fileHash"];
    //        const std::string &userIdStr = currentPermissionRow["userId"];
    //        const std::string &signatureStr = currentPermissionRow["signature"];

    //        const std::string &updateQuery =
    //            (std::stringstream() << "UPDATE " << Config::DataStorage::permissionTable << " SET
    //            permission = '"
    //                                 << newPermissionStr << "', signature = '" << newSignatureStr
    //                                 << "' WHERE fileHash = '" << fileHashStr << "' AND userId = '" <<
    //                                 userIdStr
    //                                 << "' AND signature = '" << signatureStr << "'")
    //                .str();

    //        if (!m_db.update(updateQuery)) {
    //            qDebug() << __FUNCTION__ << " Update quefy failure: " << updateQuery.c_str();
    //            return false;
    //        }
    //    }

    return true;
}

PermissionManager::Permission PermissionManager::getUserPermission(const QString &userId,
                                                                   const QString &fileHash) {
    //    DBRow currentPermissionRow = findDBRow(userId, fileHash);

    //    if (currentPermissionRow.empty()) {
    //        return Permission::NoPermission;
    //    }

    //    const QString &currentPermissionStr = currentPermissionRow["permission"].c_str();
    //    const auto currentPermissionInd = permissions.indexOf(currentPermissionStr);
    //    const Permission currentPermission = Permission(currentPermissionInd);

    //    if (currentPermission != Permission::Read && currentPermission != Permission::Write
    //        && currentPermission != Permission::Delete && currentPermission != Permission::Edit) {
    //        qDebug() << __FUNCTION__ << " Invalid permission value '" << currentPermissionStr << "'";
    //        return Permission::NoPermission;
    //    }

    //    return currentPermission;
    return Permission::NoPermission;
}

// QString PermissionManager::createDirectory(const Actor<KeyPrivate> &actor) {
//    qDebug() << __FUNCTION__;

//    QString targetDirPath = makeServiceDirPath(actor);
//    if (!QDir().mkpath(targetDirPath)) {
//        qDebug() << "DFSController: createDirectory: DFS actor dir create error:" << targetDirPath;
//        return QString();
//    }

//    return targetDirPath;
//}

// QString PermissionManager::makeActorDirPath(const Actor<KeyPrivate> &actor) {
//    return FileSystem::pathConcat(FileSystem::pathConcat(QCoreApplication::applicationDirPath(), RootDir),
//                                  actor.id().toString());
//}

// QString PermissionManager::makeServiceDirPath(const Actor<KeyPrivate> &actor) {
//    return FileSystem::pathConcat(FileSystem::pathConcat(QCoreApplication::applicationDirPath(), RootDir),
//                                  ServiceDir);
//}

DBRow PermissionManager::makeDBRow(const QString &fileHash, const Permission permission,
                                   const QString &userId, const QString &signature) {
    return { { "fileHash", fileHash.toStdString() },
             { "permission", permissions[permission].toStdString() },
             { "userId", userId.toStdString() },
             { "signature", signature.toStdString() } };
}

// DBRow PermissionManager::findDBRow(const QString &userId, const QString &fileHash) {
//    const std::string selectQuery =
//        (std::stringstream() << "SELECT * FROM " << Config::DataStorage::permissionTable
//                             << " WHERE fileHash = '" << fileHash.toStdString() << "' AND userId = '"
//                             << userId.toStdString() << "'")
//            .str();

//    const auto &rowList = m_db.select(selectQuery);
//    if (rowList.size() > 1) {
//        qDebug() << __FUNCTION__ << " Invalid DB content, entry can't be duplicated";
//        QCoreApplication::exit(DBPermissionEntryDuplicate);
//    } else if (rowList.size() == 1) {
//        return rowList[0];
//    }

//    qDebug() << __FUNCTION__ << "User " << userId << " has no access to the file " << fileHash;

//    return DBRow();
//}
