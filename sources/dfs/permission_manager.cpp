/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include "dfs/permission_manager.h"

const QString PermissionManager::PermissionFileName = ".perm";
const QString PermissionManager::ServiceDir         = "Service";
const QString PermissionManager::RootDir            = "dfs";

PermissionManager::PermissionManager(QObject *parent)
    : QObject(parent) {
}

PermissionManager::~PermissionManager() {
}

bool PermissionManager::initPermissionDB(const Actor<KeyPrivate> &actor) {
    //    eLog("{} DB initialization", __FUNCTION__);

    //    createDirectory(actor);

    //    QString serviceDirPath = makeServiceDirPath(actor);
    //    QString permissionFilePath = FileSystem::pathConcat(serviceDirPath, PermissionFileName);

    //    const bool dbExists = QFileInfo::exists(permissionFilePath);

    //    if (!m_db.open(permissionFilePath.toStdString()))
    //    {
    //        eLog("{} Can't open the permission DB", __FUNCTION__);
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
    //        eLog("{}  Delete legacy table", __FUNCTION__);

    //        if(getHighestPermission(actor.idStd().c_str(), PermissionFileName) != Permission::Edit)
    //        {
    //            // Allow edit file
    //            if(!m_db.insert(Config::DataStorage::permissionTable, rowList[1]))
    //            {
    //                eLog("{} Insert row failure", __FUNCTION__);
    //                return false;
    //            }
    //        }
    //    }

    //    if(!dbExists)
    //    {
    //        eLog("{} Create permission table", __FUNCTION__);

    //        if(!m_db.createTable(Config::DataStorage::permissionTableCreate))
    //        {
    //            eLog("{} Create table failure", __FUNCTION__);
    //            QCoreApplication::exit(DBCreateTableError);
    //        }

    //        for(const auto & row: rowList)
    //        {
    //            if(!m_db.insert(Config::DataStorage::permissionTable, row))
    //            {
    //                eLog("{} Insert row failure", __FUNCTION__);
    //                return false;
    //            }
    //        }
    //    }
    return true;
}

PermissionManager::Permission
PermissionManager::getHighestPermission(const QString &userId, const QString &fileHash) {
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

PermissionManager::Permission
PermissionManager::getPermission(const Actor<KeyPrivate> &actor, const GetPermissionMsg &msg) {
    const QString &userId   = msg.userId.c_str();
    const QString &fileHash = msg.fileHash.c_str();

    Permission permFilePermission = getHighestPermission(actor.id().to_string().c_str(), PermissionFileName);

    if (permFilePermission == Read || permFilePermission == Write || permFilePermission == Delete
        || permFilePermission == Edit) {
        Permission targetFilePermission = getHighestPermission(userId, fileHash);

        return targetFilePermission;
    }

    eLog("{} Actor {} has no permission to read the Permissions file", __FUNCTION__, actor.id().to_string().c_str());
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
    //        eLog("{} Invalid permission requested", __FUNCTION__);
    //        return false;
    //    }

    //    Permission actorPermission = getUserPermission(actor.idStd().c_str(), PermissionFileName);

    //    Permission userPermission = getPermission(actor, { userId.toStdString(), fileHash.toStdString() });

    //    if (userPermission == newPermission) {
    //        eLog("{} User  {}  already has correct permission", __FUNCTION__, userId);
    //        return true;
    //    }

    //    DBRow currentPermissionRow = findDBRow(userId, fileHash);

    //    // Delete file from the DB
    //    if (newPermission == Permission::NoPermission) {
    //        if (!(actorPermission == Delete || actorPermission == Edit)) {
    //            eLog("{} Actor {} has no permission to change the Permissions file", __FUNCTION__, actor.idStd().c_str()
    //);
    //            return false;
    //        }

    //        if (currentPermissionRow.empty()) {
    //            eLog("{} Permission already deleted from the DB", __FUNCTION__);
    //            return true;
    //        }

    //        if (!m_db.deleteRow(Config::DataStorage::permissionTable, currentPermissionRow)) {
    //            eLog("{}  Delete DB entry failure.", __FUNCTION__);
    //            return false;
    //        }
    //    }

    //    // Update PERMISSION and SIGNATURE for the existing DB entry
    //    const std::string &newPermissionStr = permissions[newPermission].toStdString();
    //    const std::string &newSignatureStr =
    //        actor.key().sign(QByteArray::fromStdString(newPermissionStr)).toStdString();

    //    if (!(actorPermission == Write || actorPermission == Edit)) {
    //        eLog("{}  Actor  {}  has no permission to change the Permissions file", __FUNCTION__, actor.idStd().c_str()
    //);
    //        return false;
    //    }

    //    // Create new DB entry
    //    if (currentPermissionRow.empty()) {
    //        eLog("{} Add new entry to the Permissions: User  {} {} {}", __FUNCTION__, userId, ", file "
    //, fileHash);

    //        const auto &signature = actor.key().sign(QByteArray::fromStdString(newPermissionStr));

    //        DBRow row = makeDBRow(fileHash, newPermission, userId, newSignatureStr.c_str());

    //        if (!m_db.insert(Config::DataStorage::permissionTable, row)) {
    //            eLog("{} Insert row failure", __FUNCTION__);
    //            return false;
    //        }
    //    } else // Update existing entry
    //    {
    //        const std::string &fileHashStr = currentPermissionRow["hash"];
    //        const std::string &userIdStr = currentPermissionRow["userId"];
    //        const std::string &signatureStr = currentPermissionRow["signature"];

    //        const std::string &updateQuery =
    //            (std::stringstream() << "UPDATE " << Config::DataStorage::permissionTable << " SET
    //            permission = '"
    //                                 << newPermissionStr << "', signature = '" << newSignatureStr
    //                                 << "' WHERE hash = '" << fileHashStr << "' AND userId = '" <<
    //                                 userIdStr
    //                                 << "' AND signature = '" << signatureStr << "'")
    //                .str();

    //        if (!m_db.update(updateQuery)) {
    //            eLog("{}  Update quefy failure:  {}", __FUNCTION__, updateQuery.c_str());
    //            return false;
    //        }
    //    }

    return true;
}

PermissionManager::Permission
PermissionManager::getUserPermission(const QString &userId, const QString &fileHash) {
    //    DBRow currentPermissionRow = findDBRow(userId, fileHash);

    //    if (currentPermissionRow.empty()) {
    //        return Permission::NoPermission;
    //    }

    //    const QString &currentPermissionStr = currentPermissionRow["permission"].c_str();
    //    const auto currentPermissionInd = permissions.indexOf(currentPermissionStr);
    //    const Permission currentPermission = Permission(currentPermissionInd);

    //    if (currentPermission != Permission::Read && currentPermission != Permission::Write
    //        && currentPermission != Permission::Delete && currentPermission != Permission::Edit) {
    //        eLog("{}  Invalid permission value ' {} '", __FUNCTION__, currentPermissionStr);
    //        return Permission::NoPermission;
    //    }

    //    return currentPermission;
    return Permission::NoPermission;
}

// QString PermissionManager::createDirectory(const Actor<KeyPrivate> &actor) {
//    eLog("{}", __FUNCTION__);

//    QString targetDirPath = makeServiceDirPath(actor);
//    if (!QDir().mkpath(targetDirPath)) {
//        eLog("DFSController: createDirectory: DFS actor dir create error: {}", targetDirPath);
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

DbRow PermissionManager::makeDBRow(
    const QString   &fileHash,
    const Permission permission,
    const QString   &userId,
    const QString   &signature) {
    return { { "fileHash", fileHash.toStdString() },
             { "permission", permissions[permission].toStdString() },
             { "userId", userId.toStdString() },
             { "signature", signature.toStdString() } };
}

// DBRow PermissionManager::findDBRow(const QString &userId, const QString &fileHash) {
//    const std::string selectQuery =
//        (std::stringstream() << "SELECT * FROM " << Config::DataStorage::permissionTable
//                             << " WHERE hash = '" << fileHash.toStdString() << "' AND userId = '"
//                             << userId.toStdString() << "'")
//            .str();

//    const auto &rowList = m_db.select(selectQuery);
//    if (rowList.size() > 1) {
//        eLog("{} Invalid DB content, entry can't be duplicated", __FUNCTION__);
//        QCoreApplication::exit(DBPermissionEntryDuplicate);
//    } else if (rowList.size() == 1) {
//        return rowList[0];
//    }

//    eLog("{} User {} has no access to the file  {}", __FUNCTION__, userId, fileHash);

//    return DBRow();
//}
