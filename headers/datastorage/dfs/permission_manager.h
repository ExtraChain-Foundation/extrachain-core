#pragma once

#include <QObject>

#include "datastorage/actor.h"
#include "dfs_controller.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"

typedef std::unordered_map<std::string, std::string> FileDataObject;

using DFS::Basic::CriticalErrors;
using DFS::Permission::AddPermission;
using DFS::Permission::Permission;
using DFS::Permission::PermissionMode;
using DFS::Permission::PermissionValue;
using DFS::Permission::RemovePermission;
using DFS::Permission::UpdatePermission;

class EXTRACHAIN_EXPORT PermissionManager : public QObject {
  Q_OBJECT

public:
  PermissionManager(ExtraChainNode *node);
  ~PermissionManager();

  CriticalErrors initPermission(const std::string &userId,
                                const std::string &fileHash,
                                PermissionMode &permissionMode,
                                DFS::Permission::AddPermission &permissionData);
  CriticalErrors
  updatePermission(const DFS::Permission::UpdatePermission &permissionData);
  CriticalErrors
  savePermission(const DFS::Permission::AddPermission &permissionData);
  CriticalErrors savePermission(const std::string &userId,
                                const std::string &fileHash,
                                const std::string &document);
  CriticalErrors rename(const std::string &oldFileHash,
                        const AddPermission &permissionData);
  CriticalErrors remove(const RemovePermission &rmPermission);
  void addPermission(PermissionMode &permissionMode,
                     const Permission &permission);
  void removePermission(DFS::Permission::PermissionMode &permissionMode,
                        const Permission &permission);

  bool isServiceable(const PermissionMode &permissionMode) const;
  bool isReadable(const PermissionMode &permissionMode) const;
  bool isWritable(const PermissionMode &permissionMode) const;
  bool isRemovable(const PermissionMode &permissionMode) const;
  bool isEditable(const PermissionMode &permissionMode) const;
  bool isCustomizable(const PermissionMode &permissionMode) const;
  bool noPermission(const PermissionMode &permissionMode) const;
  bool hasPermission(const PermissionMode &permissionMode) const;

  std::vector<std::string> searchFile(const std::string &dirPath,
                                      const std::string &partFileName);

  void sign(const Actor<KeyPrivate> &actor, AddPermission &permissionData);
  bool verify(const Actor<KeyPublic> &actor,
              const AddPermission &permissionData);

  void serializePermissionData(const std::string data, uint64_t position,
                               AddPermission perm);

  std::string getDataByValue(const AddPermission &perm, const int &value);

  std::string makePermissionFileName(const std::string &actorId,
                                     const std::string &hashFile);

protected:
  FileDataObject makeFileData(const std::string &actor, const std::string &path,
                              const std::string &fileHash,
                              const PermissionMode &permissionMode,
                              const std::string &userId,
                              const std::string &signature);
  std::string makeTempFileName(const std::string &hashFile);
  QJsonDocument makePermissionJsonDocument(const AddPermission &permissionData);
  QJsonDocument
  makePermissionJsonDocument(const UpdatePermission &permissionData);
  QJsonDocument makePermissionJsonDocument(const std::string &userId,
                                           const std::string &fileHash,
                                           PermissionMode &permissionMode);
  std::string makeData(const AddPermission &permissionData);

private:
  ExtraChainNode *node;
};
