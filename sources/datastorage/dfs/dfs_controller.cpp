#include "datastorage/dfs/dfs_controller.h"

long long DFSController::getBytesLimit() const {
    return bytesLimit;
}

void DFSController::setBytesLimit(long long newBytesLimit) {
    bytesLimit = newBytesLimit;
}

DFSController::DFSController(std::shared_ptr<ExtraChainNode> Node, QObject *parent)
    : QObject(parent) {
    node = Node;
    sizeTaken = calculateSizeTaken();
}

DFSController::~DFSController() {
}

std::string DFSController::addLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath,
                                        std::string targetVirtualFilePath, DFS::Encryption securityLevel) {
    std::string pathDelim = Utils::getPlatformDelimeter();
    std::string newFilePath = filePath;
    std::string newTargetVirtualFilePath = targetVirtualFilePath;

    // TODO: error description
    if (std::filesystem::file_size(newFilePath) >= bytesLimit - sizeTaken) {
        return "";
    }

    if (securityLevel == DFS::Encryption::Encrypted) {
        std::string fname = std::filesystem::path(filePath).stem().generic_string();
        newFilePath = "temp";
        std::filesystem::create_directories(newFilePath);
        newFilePath = newFilePath + pathDelim + fname;
        actor.key().encryptFile(filePath, newFilePath);

        std::filesystem::path nvp = targetVirtualFilePath;
        std::filesystem::path nfn = nvp.filename();
        nvp.remove_filename();
        nvp /= "secured";
        nvp /= nfn;
        newTargetVirtualFilePath = nvp.string();
    }
    std::string fileHash = Utils::calcKeccakForFile(newFilePath);
    std::filesystem::path placeInDFS =
        DFS::Basic::fsActrRoot + pathDelim + actor.id().toStdString() + pathDelim + fileHash;
    std::filesystem::copy(newFilePath, placeInDFS);
    DFS::Packets::AddFileMessage msg;
    msg.Actor = actor.id().toStdString();
    msg.FileHash = fileHash;
    msg.Path = newTargetVirtualFilePath;
    msg.Size = std::filesystem::file_size(newFilePath);

    return addFile(msg, false);
}

std::string DFSController::addFile(const DFS::Packets::AddFileMessage &msg, bool loadBytes) {
    std::string pathDelim = Utils::getPlatformDelimeter();
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::create_directories(DFS::Basic::serviceDfsPath + pathDelim + msg.Actor);

    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::create_directories(DFS::Basic::fsActrRoot + pathDelim + msg.Actor);

    std::string realFilePath = DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    std::filesystem::create_directories(DFS::Basic::fsActrRoot + pathDelim + msg.Actor);

    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }

    auto existingRowsActrDirFile = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);
    auto existingRowsLocalDirFile = DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, msg.FileHash);
    if (existingRowsActrDirFile.size() != 0 && existingRowsLocalDirFile.size() != 0) {
        actrDirFile.close();
        localDirFile.close();
        return msg.FileHash;
    }
    actrDirFile.query(DFS::Tables::ActorDirFile::CreateTableQuery);

    auto result = actrDirFile.select(DFS::Tables::filesTableLast);
    auto prevRowOpt = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileHash = prevRowOpt ? prevRowOpt->at("fileHash") : "";

    const DBRow rowData = makeActrDirDBRow(msg.FileHash, lastFileHash, msg.Path, msg.Size);

    if (!actrDirFile.insert(DFS::Tables::ActorDirFile::TableName, rowData)) {
        qDebug() << "DFSController: addFile: insert failed:" << actrDirFile.file().c_str() << " :"
                 << DFS::Tables::ActorDirFile::TableName.c_str();
        return "";
    }
    actrDirFile.close();

    localDirFile.query(DFS::Tables::LocalDirFile::CreateTableQuery);

    DBRow rowDataWithSegments;
    if (loadBytes) {
        rowDataWithSegments = makeLocalDirDBRow(msg.FileHash, lastFileHash, msg.Path, -1, -1, msg.Size);
    } else {
        rowDataWithSegments = makeLocalDirDBRow(msg.FileHash, lastFileHash, msg.Path, 0, msg.Size, msg.Size);
    }
    if (!localDirFile.insert(DFS::Tables::LocalDirFile::TableName, rowDataWithSegments)) {
        qDebug() << "DFSController: addFile: insert failed:" << localDirFile.file().c_str() << " :"
                 << DFS::Tables::LocalDirFile::TableName.c_str();
        return "";
    }
    localDirFile.close();
    if (loadBytes) {
        if (msg.Size >= bytesLimit - sizeTaken) {
            return msg.FileHash;
        } else {
            // request segments
        }
        // TODO: init segment loading
    }
    return msg.FileHash;
}

std::string DFSController::getFileFromStorage(ActorId owner, std::string fileHash) {
    Actor<KeyPrivate> localOwner = node->accountController()->getActor(owner);
    std::string pathDelim = Utils::getPlatformDelimeter();
    std::filesystem::path realFilePath =
        DFS::Basic::fsActrRoot + pathDelim + owner.toStdString() + pathDelim + fileHash;
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + owner.toStdString() + pathDelim + DFS::Basic::fsMapName;
    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + owner.toStdString() + pathDelim + DFS::Basic::fsMapName;
    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, fileHash);
    std::vector<DBRow> localDirData = DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, fileHash);
    std::filesystem::path tempFilePath = "temp" + pathDelim + owner.toStdString();
    if (actrDirData.size() > 0 && localDirData.size() > 0) {
        std::filesystem::path virtualFilePath = actrDirData[0].at("filePath");
        if ((virtualFilePath.end()--)->string() == "secured") {
            if (!localOwner.empty()) {
                std::filesystem::create_directories(tempFilePath);
                tempFilePath /= virtualFilePath.filename();
                localOwner.key().decryptFile(realFilePath, tempFilePath);
                return tempFilePath.string();
            }
        }
    }

    return realFilePath.string();
}

//
// [Before]
//
//    | fileHash | fileHashPrev | filePath
// ------------------------------------------
//  0 | 11111111 |              | filePath_1
//  1 | 22222222 | 11111111     | filePath_2
//  2 | 33333333 | 22222222     | filePath_3
//
// Remove by hash: 22222222
//
// [After]
//
//    | fileHash | fileHashPrev | filePath
// ------------------------------------------
//  0 | 11111111 |              | filePath_1
//  1 | 33333333 | 11111111     | filePath_3

bool DFSController::removeFile(const Actor<KeyPrivate> &actor, const DFS::Packets::RemoveFileMessage &msg) {
    qDebug() << "DFSController: removeFile:" << msg.FileHash.c_str();
    std::string pathDelim = Utils::getPlatformDelimeter();
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::path realFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);
    std::vector<DBRow> localDirData =
        DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, msg.FileHash);
    std::string prevHash;
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            prevHash = it->at("fileHashPrev");
            actrDirFile.deleteRow(DFS::Tables::ActorDirFile::TableName, *it);
            if (!std::filesystem::remove(realFilePath)) {
                qDebug() << "File removal by path " << realFilePath.c_str() << " failed";
                return false;
            }
        }
        if ((it->at("fileHashPrev") == msg.FileHash) && (!prevHash.empty())) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHashPrev = " + "'"
                               + prevHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }
    for (auto it = localDirData.begin(); it < localDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            prevHash = it->at("fileHashPrev");
            localDirFile.deleteRow(DFS::Tables::LocalDirFile::TableName, *it);
            if (!std::filesystem::remove(realFilePath)) {
                qDebug() << "File removal by path " << realFilePath.c_str() << " failed";
                return false;
            }
        }
        if ((it->at("fileHashPrev") == msg.FileHash) && (!prevHash.empty())) {
            localDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName
                                + " SET fileHashPrev = " + "'" + prevHash + "' " + "WHERE "
                                + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }
    actrDirFile.close();
    localDirFile.close();

    return true;
}

std::string DFSController::insertFragment(const DFS::Packets::EditSegmentMessage &msg) {
    qDebug() << "DFSController: editFile:" << msg.FileHash.c_str();
    std::string pathDelim = Utils::getPlatformDelimeter();
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::path realFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);
    std::vector<DBRow> localDirData =
        DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, msg.FileHash);

    if (actrDirData.empty() || localDirData.empty()) {
        qDebug() << "DFSController: editFile: Skipped because of empty result";
        return "";
    }

    if ((actrDirData.size() > 2) || (localDirData.size() > 2)) {
        qDebug() << "DFSController: editFile: Query select failed: Query result has unsupported size:"
                 << actrDirData.size();
        return "";
    }
    for (auto it = localDirData.begin(); it < localDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            if ((std::stoul(it->at("fileSegmentBegin")) > msg.Offset)
                || (std::stoul(it->at("fileSegmentEnd")) < (msg.Offset + msg.Data.size()))) {
                // TODO: request affected chunks

                return msg.FileHash;
            }
        }
    }

    insertDataChunk(msg.Data, msg.Offset, realFilePath);
    std::string newFileHash = Utils::calcKeccakForFile(realFilePath.string());
    unsigned int newFileSize = std::filesystem::file_size(realFilePath);
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHash = " + "'"
                               + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                               + "'");
        }
        if (it->at("fileHashPrev") == msg.FileHash) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHashPrev = " + "'"
                               + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                               + "'");
        }
    }
    for (auto it = localDirData.begin(); it < localDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            localDirFile.update("UPDATE " + DFS::Tables::LocalDirFile::TableName + " SET fileHash = " + "'"
                                + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                                + "'");
        }
        if (it->at("fileHashPrev") == msg.FileHash) {
            localDirFile.update("UPDATE " + DFS::Tables::LocalDirFile::TableName
                                + " SET fileHashPrev = " + "'" + newFileHash + "' " + "WHERE "
                                + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }
    return newFileHash;
}

bool DFSController::insertDataChunk(std::string data, long long position, std::filesystem::path file) {
    std::string pathDelim = Utils::getPlatformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream ofs(tempFilePath.string());
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    unsigned long long fz = std::filesystem::file_size(file);
    ofs.write(data.c_str(), data.size()); // add data to new temp file
    ofs.flush();
    std::size_t i = 0;
    for (i = position; i < fz; i = i + DFS::Basic::sectionSize) { // copy old data to new temp file
        if (i + DFS::Basic::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFS::Basic::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(file, position); // cut right side from old file
    std::ofstream ofsres(file.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    unsigned long long fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DFS::Basic::sectionSize) { // copy new data to old file
        if (i + DFS::Basic::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                           DFS::Basic::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        }
    }
    ofsres.close();

    std::filesystem::remove(tempFilePath);

    return true;
}
bool DFSController::removeDataChunk(long long position, long long length, std::filesystem::path file) {
    std::string pathDelim = Utils::getPlatformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream ofs(tempFilePath.string());
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    unsigned long long fz = std::filesystem::file_size(file);
    std::size_t i = 0;
    for (i = position + length; i < fz; i = i + DFS::Basic::sectionSize) { // copy old data to new temp file
        if (i + DFS::Basic::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFS::Basic::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(file, position); // cut right side from old file
    std::ofstream ofsres(file.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    unsigned long long fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DFS::Basic::sectionSize) { // copy new data to old file
        if (i + DFS::Basic::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                           DFS::Basic::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        }
    }
    ofsres.close();

    std::filesystem::remove(tempFilePath);

    return true;
}
DBRow DFSController::makeActrDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                                      long long fileSize) {
    return { { "fileHash", fileHash },
             { "fileHashPrev", fileHashPrev },
             { "filePath", filePath },
             { "fileSize", std::to_string(fileSize) } };
}
DBRow DFSController::makeLocalDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                                       long long fileSegmentBegin, long long fileSegmentEnd,
                                       long long fileSize) {
    return { { "fileHash", fileHash },
             { "fileHashPrev", fileHashPrev },
             { "filePath", filePath },
             { "fileSegmentBegin", std::to_string(fileSegmentBegin) },
             { "fileSegmentEnd", std::to_string(fileSegmentEnd) },
             { "fileSize", std::to_string(fileSize) } };
}

unsigned long long DFSController::calculateSizeTaken() {
    return std::filesystem::file_size(DFS::Basic::fsActrRoot);
}

std::string DFSController::extractFragment(boost::interprocess::file_mapping fmapTarget,
                                           unsigned long long fragmentSize, unsigned long long offset) {
    boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, offset,
                                                   fragmentSize);
    char *rr_ptr = static_cast<char *>(rightRegion.get_address());
    return std::string(rr_ptr, fragmentSize);
}

std::string DFSController::deleteFragment(const DFS::Packets::DeleteSegmentMessage &msg) {
    std::string pathDelim = Utils::getPlatformDelimeter();
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::path realFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);
    std::vector<DBRow> localDirData =
        DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, msg.FileHash);

    if (actrDirData.empty() || localDirData.empty()) {
        qDebug() << "DFSController: editFile: Skipped because of empty result";
        return "";
    }

    if ((actrDirData.size() > 2) || (localDirData.size() > 2)) {
        qDebug() << "DFSController: editFile: Query select failed: Query result has unsupported size:"
                 << actrDirData.size();
        return "";
    }
    for (auto it = localDirData.begin(); it < localDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            if ((std::stoul(it->at("fileSegmentBegin")) > msg.Offset)
                || (std::stoul(it->at("fileSegmentEnd")) < (msg.Offset + msg.Size))) {
                return msg.FileHash;
            }
        }
    }
    removeDataChunk(msg.Offset, msg.Size, realFilePath);
    std::string newFileHash = Utils::calcKeccakForFile(realFilePath.string());
    unsigned int newFileSize = std::filesystem::file_size(realFilePath);
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHash = " + "'"
                               + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                               + "'");
        }
        if (it->at("fileHashPrev") == msg.FileHash) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHashPrev = " + "'"
                               + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                               + "'");
        }
    }
    for (auto it = localDirData.begin(); it < localDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            localDirFile.update("UPDATE " + DFS::Tables::LocalDirFile::TableName + " SET fileHash = " + "'"
                                + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                                + "'");
        }
        if (it->at("fileHashPrev") == msg.FileHash) {
            localDirFile.update("UPDATE " + DFS::Tables::LocalDirFile::TableName
                                + " SET fileHashPrev = " + "'" + newFileHash + "' " + "WHERE "
                                + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }
    return newFileHash;
}
