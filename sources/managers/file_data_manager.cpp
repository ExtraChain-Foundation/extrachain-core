#include "managers/file_data_manager.h"
#include "datastorage/actor.h"
#include "utils/dfs_utils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

FileDataManager::FileDataManager(QObject *parent)
    : QObject(parent) {
    updateAllTree();
}

QJsonDocument FileDataManager::getFileTree(std::string actorId, const bool &shouldUpdateList) {
    QJsonDocument document;
    QJsonArray array;

    if (actorId.empty() && savedActorId.empty()) {
        qDebug() << "actor id and saved actor id are empty.";
        return document;
    }

    if (actorId.empty()) {
        actorId = savedActorId;
    }

    if (shouldUpdateList || files.empty())
        files = updateFileList(actorId);

    if (!shouldUpdateList) {
        files = cachedData[actorId];
    }

    for (const auto &file : files) {
        QJsonObject object;
        object.insert(name_file, QString::fromStdString(file.nameFile));
        object.insert(path_file, QString::fromStdString(file.pathFile));
        object.insert(status, file.status);
        array.push_back(object);
    }
    document.setArray(array);

    return document;
}

std::vector<FileData> FileDataManager::updateFileList(const std::string &actorId) {
    const auto pathToActorFolder = DFS::Path::actorPath(ActorId(actorId));
    std::vector<FileData> fileStructs;

    for (const auto &entry : std::filesystem::directory_iterator(pathToActorFolder)) {
        const auto nameFile = entry.path().filename();
        if (nameFile == DFSB::fsMapName || nameFile.extension() == DFSF::Extension)
            continue;

        FileStatus status = FileStatus::None;
        const auto pathToStorjFile =
            pathToActorFolder.string() + Utils::platformDelimeter() + nameFile.string() + DFSF::Extension;

        if (std::filesystem::exists(pathToStorjFile)) {
            DBConnector db(pathToStorjFile);
            if (db.open()) {
                auto countRow = db.select(DFSF::GetCountFragmants)[0];
                if (std::stoi(countRow["COUNT(size)"]) == 0) {
                    status = FileStatus::NotLoaded;
                } else {
                    auto rows = db.select(DFSF::GetSizeFragmants);
                    if (!rows.empty()) {
                        const int sizeFragments = std::stoi(rows.at(0)["SUM(size)"]);
                        const auto fileSize = entry.file_size();
                        if (fileSize == sizeFragments) {
                            status = FileStatus::Downloaded;
                        } else if (fileSize > sizeFragments) {
                            status = FileStatus::NotLoaded;
                        }
                    }
                }
                db.close();
            }
        }

        FileData fileStruct =
            FileData { .nameFile = entry.path().filename().string(), .pathFile = entry.path().string(), .status = status };
        fileStructs.emplace_back(fileStruct);
    }

    // save to cache
    cachedData[actorId] = fileStructs;

    return fileStructs;
}

const std::map<std::string, std::vector<FileData>> &FileDataManager::getCachedData() const {
    return cachedData;
}

bool FileDataManager::updateStatusByNameStatus(const std::string &nameFile, const FileStatus &newStatus) {
    const auto fileData = std::find_if(files.begin(), files.end(), [&nameFile](const FileData &fileStruct) {
        return nameFile == fileStruct.nameFile;
    });

    if (fileData != files.end()) {
        fileData->status = newStatus;
        emit statusChanged(*fileData);
        return true;
    }
    return false;
}

QJsonObject FileDataManager::getFileDataByName(const std::string &nameFile, const bool &shouldUpdateList) {
    if (shouldUpdateList)
        files = updateFileList(savedActorId);

    const auto fileData = std::find_if(files.begin(), files.end(), [&nameFile](const FileData &fileStruct) {
        return nameFile == fileStruct.nameFile;
    });

    QJsonObject object;
    object.insert(name_file, QString::fromStdString(fileData->nameFile));
    object.insert(path_file, QString::fromStdString(fileData->pathFile));
    object.insert(status, fileData->status);
    return object;
}

QJsonDocument FileDataManager::getFilesTreeByStatus(const FileStatus &fileStatus,
                                                    const bool &shouldUpdateList) {
    if (shouldUpdateList)
        files = updateFileList(savedActorId);

    QJsonDocument document;
    QJsonArray array;

    for (const auto &file : files) {
        if (file.status != fileStatus)
            continue;

        QJsonObject object;
        object.insert(name_file, QString::fromStdString(file.nameFile));
        object.insert(path_file, QString::fromStdString(file.pathFile));
        object.insert(status, file.status);
        array.push_back(object);
    }
    document.setArray(array);
    return document;
}

void FileDataManager::setActorId(const std::string &actorId) {
    savedActorId = actorId;
}

void FileDataManager::updateAllTree() {
    for (const auto &entry : std::filesystem::directory_iterator(DFSB::fsActrRoot)) {
        if (entry.is_directory()) {
            const std::string actorId = entry.path().filename().string();
            updateFileList(actorId);
        }
    }
}
