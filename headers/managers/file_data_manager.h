#pragma once

#include "datastorage/actor.h"

#include <QObject>
#include <vector>

class QJsonDocument;
class QJsonObject;
class QJsonArray;

static const char *name_file = "name_file";
static const char *path_file = "path_file";
static const char *status    = "status";

enum FileStatus {
    None,
    NotLoaded,
    Loading,
    Downloaded
};

struct FileData {
    std::string nameFile = "";
    std::string pathFile = "";
    FileStatus  status   = FileStatus::None;
};

class FileDataManager : public QObject {
    Q_OBJECT

public:
    FileDataManager(QObject *parent = nullptr);
    QJsonDocument getFileTree(ActorId actorId = ActorId(), const bool &shouldUpdateList = false);
    bool          updateStatusByNameStatus(const std::string &nameFile, const FileStatus &newStatus);
    QJsonObject   getFileDataByName(const std::string &nameFile, const bool &shouldUpdateList = false);
    QJsonDocument getFilesTreeByStatus(const FileStatus &fileStatus, const bool &shouldUpdateList = false);
    void          setActorId(const ActorId &actorId);
    void          updateAllTree();

    const std::map<ActorId, std::vector<FileData>> &getCachedData() const;

signals:
    void structuraChanged(FileData fileStruct);
    void statusChanged(FileData fileStruct);

protected:
    std::vector<FileData> updateFileList(const ActorId &actorId);

private:
    std::vector<FileData>                    files;
    std::map<ActorId, std::vector<FileData>> cachedData;
    ActorId                                  savedActorId;
};
