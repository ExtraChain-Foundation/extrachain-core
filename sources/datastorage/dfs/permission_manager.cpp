#include "datastorage/dfs/permission_manager.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>
#include <QJsonObject>

PermissionManager::PermissionManager(ExtraChainNode *node)
    : node(node)
{}

PermissionManager::~PermissionManager() {}

CriticalErrors PermissionManager::initPermission(const std::string& userId, const std::string& fileHash, DFS::Permission::PermissionMode &permissionMode)
{
    const std::string pathToPermissionFile = makeFileName(userId, fileHash);

    if(!std::filesystem::exists(pathToPermissionFile))
        return CriticalErrors::NoFile;

    std::ifstream permissionFile;
    permissionFile.open(pathToPermissionFile, std::ios_base::in);
    if (!permissionFile.is_open()) {
        return CriticalErrors::NotOpenFile;
    }

    std::string content;
    std::string tp;
    while(getline(permissionFile, tp)) {
        content.append(tp);
    }
    permissionFile.close();

    QJsonParseError readJsonError;
    const auto document = QJsonDocument::fromJson(content.data(), &readJsonError);

    if(readJsonError.error != QJsonParseError::NoError)
        return CriticalErrors::ParseError;

    const auto object = document.object();

    permissionData.userId = object[DFS::Permission::userID.c_str()].toString().toStdString();
    permissionData.fileHash = object[DFS::Permission::fileHash.c_str()].toString().toStdString();
    permissionData.permissionValue = object[DFS::Permission::permissionValue.c_str()].toString().toInt();

    auto checkPermission = [&](Permission type)
    {
        if(permissionData.permissionValue >= type) {
            addPermission(permissionMode, type);
            permissionData.permissionValue -= type;
        }
    };
    checkPermission(Permission::Service);
    checkPermission(Permission::Read);
    checkPermission(Permission::Write);
    checkPermission(Permission::Remove);
    checkPermission(Permission::Edit);
    checkPermission(Permission::Custom);

    return CriticalErrors::NoError;
}

CriticalErrors PermissionManager::updatePermission(const DFS::Permission::AddPermission &permissionData)
{
    const std::string pathToPermissionFile = makeFileName(permissionData.userId, permissionData.fileHash);

    std::ofstream permissionFile;
    permissionFile.open(pathToPermissionFile, std::ios_base::out | std::ios::trunc);

         //check file exist
         //create new file is doesn't exist

    if (!permissionFile.is_open()) {
        return CriticalErrors::NotOpenFile;
    }

    const auto document = makePermissionJsonDocument(permissionData);
    permissionFile << document.toJson().toStdString().c_str();
    permissionFile.close();

    return std::filesystem::is_empty(pathToPermissionFile) ? CriticalErrors::NoUpdated : CriticalErrors::NoError;
}

CriticalErrors PermissionManager::savePermission(const DFS::Permission::AddPermission &permissionData)
{
    const std::string pathToPermissionFile = makeFileName(permissionData.userId, permissionData.fileHash);

    std::ofstream permissionFile;
    permissionFile.open(pathToPermissionFile, std::ios_base::out | std::ios::trunc);
    if (!permissionFile.is_open()) {
        return CriticalErrors::NotOpenFile;
    }

    const QJsonDocument document = makePermissionJsonDocument(permissionData);
    permissionFile << document.toJson().toStdString().c_str();
    permissionFile.close();

    return std::filesystem::is_empty(pathToPermissionFile) ? CriticalErrors::NoSaved : CriticalErrors::NoError;
}

void PermissionManager::addPermission(DFS::Permission::PermissionMode &permissionMode, const Permission &permission)
{
    permissionMode |= permission;
}

void PermissionManager::removePermission(DFS::Permission::PermissionMode &permissionMode, const Permission& permission)
{
    permissionMode ^= permission;
}

bool PermissionManager::isServiceable(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.testFlag(Permission::Service);
}

bool PermissionManager::isReadable(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.testFlag(Permission::Read);
}

bool PermissionManager::isWritable(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.testFlag(Permission::Write);
}

bool PermissionManager::isRemovable(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.testFlag(Permission::Remove);
}

bool PermissionManager::isEditable(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.testFlag(Permission::Edit);
}

bool PermissionManager::isCustomizable(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.testFlag(Permission::Custom);
}

bool PermissionManager::noPermission(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.toInt() == 0;
}

bool PermissionManager::hasPermission(const DFS::Permission::PermissionMode &permissionMode) const
{
    return permissionMode.toInt() > 0;
}

std::vector<std::string> PermissionManager::searchFileByHash(std::string& dirPath, std::string &partHash)
{
    std::vector<std::string> result;
    if(partHash.length() == 0) {
        return result;
    }

    if(partHash.length() < lastFoundedPartHashFile.length()) {
        directoryEntry.clear();
        lastFoundedPartHashFile = partHash;

        for (auto &pathToFile : std::filesystem::recursive_directory_iterator(dirPath)) {
            if (std::filesystem::is_regular_file(pathToFile) && pathToFile.path().extension() == DFS::Permission::permissionFileExtension) {
                std::stringstream ss(pathToFile.path().filename());
                std::string segment;
                std::vector<std::string> splitList;

                while(std::getline(ss, segment, '_'))
                {
                    splitList.emplace_back(segment);
                }

                const std::string hashCurrentFile = splitList.at(1);
                if(hashCurrentFile.find(partHash) != std::string::npos) {
                    result.emplace_back(pathToFile.path());
                    directoryEntry.emplace_back(pathToFile);
                }
            }
        }
    } else {
        lastFoundedPartHashFile = partHash;
        std::vector<std::filesystem::directory_entry> tmpSavedListOfDirectoryEntry;
        for (auto &pathToFile : directoryEntry) {
            std::stringstream ss(pathToFile.path().filename());
            std::string segment;
            std::vector<std::string> splitList;

            while(std::getline(ss, segment, '_'))
            {
                splitList.emplace_back(segment);
            }

            const std::string hashCurrentFile = splitList.at(1);
            if(hashCurrentFile.find(partHash) == std::string::npos) {
                remove(directoryEntry.begin(), directoryEntry.end(), pathToFile);
            }
        }

        for (auto &pathToFile : directoryEntry) {
            result.push_back(pathToFile.path());
        }
    }

    return result;
}

std::vector<std::string> PermissionManager::searchFileByName(std::string& dirPath, std::string& partUserName) {
    std::vector<std::string> result;
    if(partUserName.length() == 0) {
        return result;
    }

    if(partUserName.length() < lastFoundedPartUserName.length()) {
        directoryEntry.clear();
        lastFoundedPartUserName = partUserName;

        for (auto &pathToFile : std::filesystem::recursive_directory_iterator(dirPath)) {
            if (std::filesystem::is_regular_file(pathToFile) && pathToFile.path().extension() == DFS::Permission::permissionFileExtension) {
                const std::string fileName = pathToFile.path().filename();

                std::string fullNameUser = fileName.substr(0, fileName.find("_"));
                if(fullNameUser.find(partUserName) != std::string::npos) {
                    result.emplace_back(pathToFile.path());
                    directoryEntry.emplace_back(pathToFile);
                }
            }
        }
    } else {
        lastFoundedPartUserName = partUserName;
        std::vector<std::filesystem::directory_entry> tmpSavedListOfDirectoryEntry;
        for (auto &pathToFile : directoryEntry) {
            const std::string fileName = pathToFile.path().filename();
            std::string fullNameUser = fileName.substr(0, fileName.find("_"));
            if(fullNameUser.find(partUserName) == std::string::npos) {
                remove(directoryEntry.begin(), directoryEntry.end(), pathToFile);
            }
        }

        for (auto &pathToFile : directoryEntry) {
            result.push_back(pathToFile.path());
        }
    }

    return result;
}

void PermissionManager::sign(const Actor<KeyPrivate> &actor, DFS::Permission::AddPermission &permissionData)
{
    const auto dataForSign = makeData(permissionData);
    permissionData.signature = actor.key().sign(dataForSign);
}

bool PermissionManager::verify(const Actor<KeyPublic> &actor, const DFS::Permission::AddPermission &permissionData)
{
    const std::string dataForSign = makeData(permissionData);
    return actor.key().verify(QByteArray::fromStdString(dataForSign), QByteArray::fromStdString(permissionData.signature));
}

FileDataObject PermissionManager::makeFileData(const std::string& actor, const std::string& path,
                                               const std::string &fileHash, const DFS::Permission::PermissionMode& permissionMode,
                                               const std::string &userId, const std::string &signature)
{
    return { { "actor", actor },
             { "path", path },
             { "fileHash", fileHash },
             { "permission_value", std::to_string(permissionMode.toInt()) },
             { "userId", userId },
             { "signature", signature } };
}

std::string PermissionManager::makeFileName(const std::string &userName, const std::string &hashFile)
{
    std::stringstream stringStream;
    stringStream << userName << "_" << hashFile << "." << DFS::Permission::permissionFileExtension;
    return stringStream.str();
}

QJsonDocument PermissionManager::makePermissionJsonDocument(const DFS::Permission::AddPermission &permissionData)
{
    QJsonObject permissionObject;
    permissionObject[DFS::Permission::userID.c_str()] = permissionData.userId.c_str();
    permissionObject[DFS::Permission::fileHash.c_str()] = permissionData.fileHash.c_str();
    permissionObject[DFS::Permission::permissionValue.c_str()] = std::to_string(permissionData.permissionValue).c_str();

    return QJsonDocument(permissionObject);
}

QJsonDocument PermissionManager::makePermissionJsonDocument(const std::string &userId, const std::string &fileHash, DFS::Permission::PermissionMode &permissionMode)
{
    QJsonObject permissionObject;
    permissionObject[DFS::Permission::userID.c_str()] = userId.c_str();
    permissionObject[DFS::Permission::fileHash.c_str()] = fileHash.c_str();
    permissionObject[DFS::Permission::permissionValue.c_str()] = std::to_string(permissionMode.toInt()).c_str();

    return QJsonDocument(permissionObject);
}

std::string PermissionManager::makeData(const DFS::Permission::AddPermission &permissionData)
{
    std::stringstream stringStream;
    stringStream << permissionData.actor << permissionData.path << permissionData.userId
                 << permissionData.fileHash << permissionData.permissionValue;
    return stringStream.str();
}
