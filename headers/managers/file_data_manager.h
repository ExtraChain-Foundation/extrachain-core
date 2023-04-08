#pragma once

#include <QObject>
#include <iostream>
#include <vector>

class QJsonDocument;
class QJsonObject;
class QJsonArray;

static const char *name_file = "name_file";
static const char *path_file = "path_file";
static const char *status = "status";

enum FileStatus {
    None,
    NotLoaded,
    Loading,
    Downloaded
};

struct FileData {
    std::string nameFile = "";
    std::string pathFile = "";
    FileStatus status = FileStatus::None;
};

class FileDataManager : public QObject {
    Q_OBJECT
public:
    FileDataManager(QObject *parent = nullptr);
    QJsonDocument getFileTree(std::string actorId = "", const bool &shouldUpdateList = false);
    bool updateStatusByNameStatus(const std::string &nameFile, const FileStatus &newStatus);
    QJsonObject getFileDataByName(const std::string &nameFile, const bool &shouldUpdateList = false);
    QJsonDocument getFilesTreeByStatus(const FileStatus &fileStatus, const bool &shouldUpdateList = false);
    void setActorId(const std::string &actorId);
    void updateAllTree();

    const std::map<std::string, std::vector<FileData> > &getCachedData() const;

signals:
    void structuraChanged(FileData fileStruct);
    void statusChanged(FileData fileStruct);

protected:
    std::vector<FileData> updateFileList(const std::string &actorId);

private:
    std::vector<FileData> files;
    std::map<std::string, std::vector<FileData>> cachedData;
    std::string savedActorId;
};
