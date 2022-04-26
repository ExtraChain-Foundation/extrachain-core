#include "datastorage/dfs/permission_manager.h"
#include <sstream>
#include <string>
#include <QJsonObject>


PermissionManager::PermissionManager(ExtraChainNode *node)
    : node(node)
{}

PermissionManager::~PermissionManager() {}

CriticalErrors PermissionManager::initPermission(const std::string& userId, const std::string& fileHash, DFS::Permission::PermissionMode &permissionMode)
{
    const std::string pathToPermissionFile = makePermissionFileName(userId, fileHash);

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
    const std::string pathToPermissionFile = makePermissionFileName(permissionData.userId, permissionData.fileHash);

    if(!std::filesystem::exists(pathToPermissionFile))
        return savePermission(permissionData);

    std::ofstream permissionFile;
    permissionFile.open(pathToPermissionFile, std::ios_base::out | std::ios::trunc);

    if (!permissionFile.is_open()) {
        return CriticalErrors::NotOpenFile;
    }

    const auto document = makePermissionJsonDocument(permissionData);
    permissionFile << document.toJson().toStdString().c_str();
    permissionFile.close();

    return std::filesystem::is_empty(pathToPermissionFile) ? CriticalErrors::NoUpdated
                                                           : CriticalErrors::NoError;
}

CriticalErrors PermissionManager::savePermission(const DFS::Permission::AddPermission &permissionData)
{
    const std::string pathToPermissionFile = makePermissionFileName(permissionData.userId, permissionData.fileHash);
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

CriticalErrors PermissionManager::rename(const std::string &oldFileHash, const DFS::Permission::AddPermission &permissionData)
{
    if(oldFileHash != permissionData.fileHash) {
        const std::string pathToPermissionFile = makePermissionFileName(permissionData.userId, oldFileHash);
        const std::string newFileName = makePermissionFileName(permissionData.userId, permissionData.fileHash);
        CriticalErrors error = savePermission(permissionData);

        if(error != CriticalErrors::NoError)
            return error;

        if(!std::filesystem::remove(pathToPermissionFile))
            return CriticalErrors::NotRemoveOldFile;

    }
    return CriticalErrors::NoError;
}

CriticalErrors PermissionManager::remove(const DFS::Permission::RemovePermission &rmPermission)
{
    const std::string pathToPermissionFile = makePermissionFileName(rmPermission.actor, rmPermission.fileHash);
    if(!std::filesystem::remove(pathToPermissionFile))
        return CriticalErrors::NotRemovePermissionFile;

    return CriticalErrors::NoError;
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

std::vector<std::string> PermissionManager::searchFile(const std::string& dirPath, const std::string& partFileName)
{
    std::vector<std::string> result;
    if(partFileName.length() == 0) {
        return result;
    }

    for (auto &pathToFile : std::filesystem::recursive_directory_iterator(dirPath)) {
        if (std::filesystem::is_regular_file(pathToFile) && pathToFile.path().extension() == DFS::Permission::permissionFileExtension) {
            const std::string fileName = pathToFile.path().filename();

            if(fileName.find(partFileName) != std::string::npos) {
                result.emplace_back(pathToFile.path());
            }
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

void PermissionManager::serializePermissionData(const std::string data, uint64_t position, DFS::Permission::AddPermission perm) {
    std::filesystem::path file = makePermissionFileName(perm.userId, perm.fileHash);
    std::string pathDelim = Utils::getPlatformDelimeter();
    std::filesystem::path tempFilePath = makeTempFileName(perm.fileHash);
    std::ofstream ofs(tempFilePath.string(), std::ios::binary | std::ios::trunc);

    ofs.write(perm.actor.c_str(), perm.actor.size());
    ofs.write(perm.path.c_str(), perm.path.size());
    ofs.write(perm.userId.c_str(), perm.userId.size());
    ofs.write(perm.fileHash.c_str(), perm.fileHash.size());
    ofs.write(perm.signature.c_str(), perm.signature.size());
    ofs.write(std::to_string(perm.permissionValue).c_str(), sizeof(perm.permissionValue));
    ofs.flush();
    ofs.close();

    qDebug() << "read data from to bits";
    qDebug() << "actor: " << QString::fromStdString(getDataByValue(perm, DFS::Permission::PermissionValue::Actor));
    qDebug() << "path: " << QString::fromStdString(getDataByValue(perm, DFS::Permission::PermissionValue::Path));
    qDebug() << "userId: " << QString::fromStdString(getDataByValue(perm, DFS::Permission::PermissionValue::UserID));
    qDebug() << "file_hash: " << QString::fromStdString(getDataByValue(perm, DFS::Permission::PermissionValue::FileHash));
    qDebug() << "signature: " << QString::fromStdString(getDataByValue(perm, DFS::Permission::PermissionValue::Signature));
    qDebug() << "p_value: " << std::stoi(getDataByValue(perm, DFS::Permission::PermissionValue::PermissionValue));
}

std::string PermissionManager::getDataByValue(const DFS::Permission::AddPermission &addPermission, const int &value) {
    const auto valueFromByte = [&]()
    {
        int res = 0;
        switch(value) {
        case DFS::Permission::PermissionValue::Actor: break;
        case DFS::Permission::PermissionValue::Path: {
            res = addPermission.actor.size();
            break;
        }
        case DFS::Permission::PermissionValue::UserID: {
            res = (addPermission.actor.size() + addPermission.path.size());
            break;
        }
        case DFS::Permission::PermissionValue::FileHash: {
            res = (addPermission.actor.size() + addPermission.path.size() + addPermission.userId.size());
            break;
        }
        case DFS::Permission::PermissionValue::Signature: {
            res = (addPermission.actor.size() + addPermission.path.size() + addPermission.userId.size() + addPermission.fileHash.size());
            break;
        }
        case DFS::Permission::PermissionValue::PermissionValue: {
            res = (addPermission.actor.size() + addPermission.path.size() + addPermission.userId.size()
                   + addPermission.fileHash.size() + addPermission.signature.size());
            break;
        }
        }
        return res;
    };

    const auto sizeOfValue = [&]()
    {
        switch(value) {
        case DFS::Permission::PermissionValue::Actor: return addPermission.actor.size();
        case DFS::Permission::PermissionValue::Path: return addPermission.path.size();
        case DFS::Permission::PermissionValue::UserID: return addPermission.userId.size();
        case DFS::Permission::PermissionValue::FileHash: return addPermission.fileHash.size();
        case DFS::Permission::PermissionValue::Signature: return addPermission.signature.size();
        case DFS::Permission::PermissionValue::PermissionValue: return sizeof(addPermission.permissionValue);
        }
    };
    std::filesystem::path tempFilePath = makeTempFileName(addPermission.fileHash);
    std::ofstream ofsres(tempFilePath.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, valueFromByte());
    std::string r = static_cast<char *>(rightRegion.get_address());
    r.resize(valueFromByte() + sizeOfValue());
    return r.c_str();
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

std::string PermissionManager::makePermissionFileName(const std::string &userName, const std::string &hashFile)
{
    std::stringstream stringStream;
    stringStream << DFS::Permission::rootDir << "/" << userName << "/"
                 << hashFile << DFS::Permission::permissionFileExtension;
    return stringStream.str();
}

std::string PermissionManager::makeTempFileName(const std::string &hashFile)
{
    std::stringstream stringStream;
    stringStream << DFS::Permission::tempDir << "/" << "tmp_"
                 << hashFile << DFS::Permission::permissionFileExtension;
    return stringStream.str();
}

QJsonDocument PermissionManager::makePermissionJsonDocument(const DFS::Permission::AddPermission &permissionData)
{
    QJsonObject permissionObject;
    permissionObject[DFS::Permission::userID.c_str()] = permissionData.userId.c_str();
    permissionObject[DFS::Permission::actor.c_str()] = permissionData.actor.c_str();
    permissionObject[DFS::Permission::path.c_str()] = permissionData.path.c_str();
    permissionObject[DFS::Permission::signature.c_str()] = permissionData.signature.c_str();
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
