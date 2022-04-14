#pragma once

#include <QObject>

#include "datastorage/actor.h"
#include "dfs_controller.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"

typedef std::unordered_map<std::string, std::string> FileDataObject;

using DFS::Permission::Permission;
using DFS::Basic::CriticalErrors;

class EXTRACHAIN_EXPORT PermissionManager : public QObject {
    Q_OBJECT

public:
    PermissionManager(ExtraChainNode *node);
    ~PermissionManager();

    CriticalErrors initPermission(const std::string& userId, const std::string& fileHash,
                                  DFS::Permission::PermissionMode &permissionMode);
    CriticalErrors updatePermission(const DFS::Permission::AddPermission &permissionData);
    CriticalErrors savePermission(const DFS::Permission::AddPermission &permissionData);

    void addPermission(DFS::Permission::PermissionMode &permissionMode, const Permission& permission);
    void removePermission(DFS::Permission::PermissionMode &permissionMode, const Permission& permission);

    bool isServiceable(const DFS::Permission::PermissionMode& permissionMode) const;
    bool isReadable(const DFS::Permission::PermissionMode &permissionMode) const;
    bool isWritable(const DFS::Permission::PermissionMode &permissionMode) const;
    bool isRemovable(const DFS::Permission::PermissionMode &permissionMode) const;
    bool isEditable(const DFS::Permission::PermissionMode &permissionMode) const;
    bool isCustomizable(const DFS::Permission::PermissionMode &permissionMode) const;
    bool noPermission(const DFS::Permission::PermissionMode &permissionMode) const;
    bool hasPermission(const DFS::Permission::PermissionMode &permissionMode) const;

    std::vector<std::string> searchFileByHash(std::string& dirPath, std::string& partHash);
    std::vector<std::string> searchFileByName(std::string& dirPath, std::string& userName);

    void sign(const Actor<KeyPrivate> &actor, DFS::Permission::AddPermission &permissionData);
    bool verify(const Actor<KeyPublic> &actor, const DFS::Permission::AddPermission &permissionData);

protected:
    FileDataObject makeFileData(const std::string& actor, const std::string& path, const std::string &fileHash,
                                const DFS::Permission::PermissionMode& permissionMode, const std::string &userId, const std::string &signature);
    std::string makeFileName(const std::string& userName, const std::string& hashFile);
    QJsonDocument makePermissionJsonDocument(const DFS::Permission::AddPermission &permissionData);
    QJsonDocument makePermissionJsonDocument(const std::string& userId, const std::string& fileHash, DFS::Permission::PermissionMode &permissionMode);
    std::string makeData(const DFS::Permission::AddPermission &permissionData);

private:
    ExtraChainNode *node;
    DFS::Permission::AddPermission permissionData;
    std::string lastFoundedPartUserName;
    std::string lastFoundedPartHashFile;
    std::vector<std::filesystem::directory_entry> directoryEntry;
};
