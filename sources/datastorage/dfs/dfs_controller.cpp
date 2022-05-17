#include "datastorage/dfs/dfs_controller.h"
#include "datastorage/dfs/fragment_storage.h"
#include "datastorage/dfs/historical_chain.h"
#include "network/network_manager.h"

DfsController::DfsController(ExtraChainNode &node, QObject *parent)
    : QObject(parent)
    , node(node) {
    std::filesystem::create_directories(DFSB::fsActrRoot);

    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    dirsFile.query(DFST::DirsFile::CreateTableQuery);

    m_sizeTaken = calculateSizeTaken();
    qDebug() << "[Dfs] Started. Current size:" << m_sizeTaken;
}

DfsController::~DfsController() {
}

void DfsController::initializeActor(const ActorId &actorId) {
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::create_directories(DFSB::fsActrRoot + pathDelim + actorId.toStdString());
    DBConnector actrDirFile = DFST::ActorDirFile::actorDbConnector(actorId.toStdString());
    actrDirFile.query(DFST::ActorDirFile::CreateTableQuery);

    //
    requestDirData(actorId);
}

std::string DfsController::addLocalFile(const Actor<KeyPrivate> &actor, const std::filesystem::path &filePath,
                                        std::string targetVirtualFilePath, DFS::Encryption securityLevel) {
    std::filesystem::path fpath = DFS_PATH::convertPathToPlatform(filePath);
    std::filesystem::path newFilePath = fpath;
    std::string newTargetVirtualFilePath = targetVirtualFilePath;

#ifdef ANDROID
    auto tempPath = "dfs/temp"
        + QString::number(QRandomGenerator::global()->bounded(1000) + QDateTime::currentMSecsSinceEpoch());
    QFile::copy(newFilePath.string().c_str(), tempPath);
    fpath = tempPath.toStdString();
    newFilePath = fpath;
#endif

    if (!std::filesystem::exists(newFilePath)) {
        qInfo() << "[Dfs] Can't load file";
        return "ErrorNotExists";
    }

    if (!std::filesystem::is_regular_file(newFilePath)) {
        qInfo() << "[Dfs] This is not a file";
        return "ErrorNotFile";
    }

    std::ifstream my_file(newFilePath);
    if (!my_file) {
        qDebug() << "Can't read";
        return "ErrorNotReadable";
    }
    my_file.close();

    // TODO: error description
    if (std::filesystem::file_size(newFilePath) >= m_bytesLimit - m_sizeTaken) {
        return "ErrorStorageFull";
    }

    if (securityLevel == DFS::Encryption::Encrypted) {
        std::wstring fname = std::filesystem::path(fpath).stem().wstring();
        newFilePath = L"temp";
        std::filesystem::create_directories(newFilePath);
        newFilePath = newFilePath.wstring() + DFSB::separator + fname;
        actor.key().encryptFile(fpath, newFilePath);

        std::filesystem::path nvp = targetVirtualFilePath;
        std::filesystem::path nfn = nvp.filename();
        nvp.remove_filename();
        nvp /= "secured";
        nvp /= nfn;
        newTargetVirtualFilePath = nvp.string();
    }

    std::string fileHash = Utils::calcHashForFile(newFilePath);
    auto fileSize = std::filesystem::file_size(newFilePath);
    std::filesystem::path placeInDFS =
        DFSB::fsActrRootW + DFSB::separator + actor.id().toString().toStdWString() + DFSB::separator;
    std::filesystem::path dfsPath = DFS_PATH::filePath(actor.id(), fileHash);

    if (std::filesystem::exists(dfsPath) && std::filesystem::file_size(dfsPath) == fileSize) {
        std::string dfsFileHash = Utils::calcHashForFile(dfsPath);
        if (fileHash == dfsFileHash) {
            qDebug() << "[DFS] File already in DFS";
            return "";
        }
    }

    try {
        std::filesystem::create_directories(placeInDFS.c_str());
        placeInDFS /= fileHash;
#ifdef ANDROID
        std::filesystem::rename(newFilePath, placeInDFS);
#else
        std::filesystem::copy(newFilePath, placeInDFS);
#endif
    } catch (std::filesystem::filesystem_error const &err) { qDebug() << "[Dfs] Copy error:" << err.what(); }

    FragmentStorage fs(actor.id(), fileHash);
    fs.initLocalFile(fileSize);
    DFSP::AddFileMessage msg = { .Actor = actor.id().toStdString(),
                                 .FileHash = fileHash,
                                 .Path = newTargetVirtualFilePath,
                                 .Size = fileSize };
    qDebug() << "AddFileMessage" << actor.id().toString() << fileHash.c_str()
             << newTargetVirtualFilePath.c_str() << fileSize;

    auto actrDirFile = DFST::ActorDirFile::actorDbConnector(actor.id().toStdString());
    auto lastFileHash = DFST::ActorDirFile::getLastHash(actrDirFile);
    const DBRow rowData = makeActrDirDBRow(msg.FileHash, lastFileHash, msg.Path, msg.Size);

    if (!actrDirFile.insert(DFST::ActorDirFile::TableName, rowData)) {
        qDebug() << "[Dfs] addFile: insert failed:" << actrDirFile.file().c_str() << " :"
                 << DFST::ActorDirFile::TableName.c_str();
        qFatal("Insert failed");
        return "";
    }
    actrDirFile.close();
    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    dirsFile.replace(
        DFST::DirsFile::TableName,
        { { "actorId", actor.id().toStdString() }, { "lastModified", rowData.at("lastModified") } });

    sendFile(actor.id(), fileHash, "");

    HistoricalChain hc((DFS_PATH::filePath(actor.id().toStdString(), fileHash).string() + DFSF::Extension),
                       fpath.string());
    hc.initLocal(actor.id().toStdString(), fileHash);

    return addFile(msg, false);
}

void DfsController::removeLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath) {
    std::string fileHash = Utils::calcHashForFile(filePath); // TODO: get hash
    DFSP::RemoveFileMessage msg = { .Actor = actor.id().toStdString(), .FileHash = fileHash };
    node.network()->send_message(msg, MessageType::DfsRemoveFile);
}

std::string DfsController::addFile(const DFSP::AddFileMessage &msg, bool loadBytes) {
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::string realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;

    if (loadBytes && std::filesystem::exists(realFilePath)) {
        qDebug() << "[Dfs] File already exists"; // temp: not correct, add calc file
        return msg.FileHash;
    }

    if (loadBytes && !std::filesystem::exists(realFilePath)) {
        std::fstream fs;
        fs.open(realFilePath, std::ios::out | std::ios::binary);
        fs.close();
    }

    DBConnector actrDirFile(actrDirFilePath);

    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }

    auto result = actrDirFile.select(DFST::filesTableLast);
    auto prevRowOpt = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileHash = prevRowOpt ? prevRowOpt->at("fileHash") : "";

    const DBRow rowData = makeActrDirDBRow(msg.FileHash, lastFileHash, msg.Path, msg.Size);

    if (!actrDirFile.insert(DFST::ActorDirFile::TableName, rowData)) {
        qDebug() << "[Dfs] addFile: insert failed:" << actrDirFile.file().c_str() << " :"
                 << DFST::ActorDirFile::TableName.c_str();
        qFatal("Error 2");
        return "";
    }
    actrDirFile.close();

    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    dirsFile.replace(DFST::DirsFile::TableName,
                     { { "actorId", msg.Actor }, { "lastModified", rowData.at("lastModified") } });

    if (loadBytes) {
        if (msg.Size >= m_bytesLimit - m_sizeTaken) {
            return msg.FileHash;
        } else {
            DFSP::RequestFileSegmentMessage reqMessage = {
                .Actor = msg.Actor, .FileHash = msg.FileHash, .Path = msg.Path, .Offset = 0
            };
            node.network()->send_message(reqMessage, MessageType::DfsRequestFileSegment,
                                         MessageStatus::Request);
        }
    }

    files[msg.Actor + msg.FileHash] = msg;
    emit added(msg.Actor, msg.FileHash, msg.Path, msg.Size);

    return msg.FileHash;
}

std::string DfsController::getFileFromStorage(ActorId owner, std::string fileHash) {
    Actor<KeyPrivate> localOwner = node.accountController()->currentProfile().getActor(owner);
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path realFilePath =
        DFSB::fsActrRoot + pathDelim + owner.toStdString() + pathDelim + fileHash;
    std::string actrDirFilePath =
        DFSB::fsActrRoot + pathDelim + owner.toStdString() + pathDelim + DFSB::fsMapName;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        qFatal("Can't open %s", actrDirFilePath.c_str());
        exit(EXIT_FAILURE);
    }

    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByHash(&actrDirFile, fileHash);
    std::filesystem::path tempFilePath = "temp" + pathDelim + owner.toStdString();
    if (actrDirData.size() > 0) {
        std::filesystem::path virtualFilePath = actrDirData.at(0).at("filePath");
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

bool DfsController::removeFile(const DFSP::RemoveFileMessage &msg) {
    qDebug() << "[Dfs] Remove file message:" << msg.FileHash.c_str();
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::filesystem::path realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);
    std::string prevHash;
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            prevHash = it->at("fileHashPrev");
            actrDirFile.deleteRow(DFST::ActorDirFile::TableName, *it);
            if (!std::filesystem::remove(realFilePath)) {
                qDebug() << "File removal by path " << realFilePath.c_str() << " failed";
                return false;
            }
        }
        if ((it->at("fileHashPrev") == msg.FileHash) && (!prevHash.empty())) {
            actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHashPrev = " + "'"
                               + prevHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }

    actrDirFile.close();

    return true;
}

std::string DfsController::insertFragment(const DFSP::SegmentMessage &msg) {
    qDebug() << "[Dfs] Edit file:" << msg.FileHash.c_str();
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::filesystem::path realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);

    if (actrDirData.empty()) {
        qDebug() << "[Dfs] editFile: Skipped because of empty result";
        qFatal("actrDirData || localDirData empty");
        return "";
    }

    if (actrDirData.size() > 2) {
        qDebug() << "[Dfs] editFile: Query select failed: Query result has unsupported size:"
                 << actrDirData.size();
        qFatal("Error 4");
        return "";
    }

    insertDataChunk(msg.Data, msg.Offset, realFilePath);

    std::string newFileHash = Utils::calcHashForFile(realFilePath.string());
    // auto newFileSize = std::filesystem::file_size(realFilePath);
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHash = " + "'"
            //                    + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
            //                    + "'");
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileSize = " + "'"
            //                    + std::to_string(newFileSize) + "' " + "WHERE " + "fileHash = " + "'"
            //                    + it->at("fileHash") + "'");
        }
        if (it->at("fileHashPrev") == msg.FileHash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHashPrev = " +
            // "'"
            //                    + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
            //                    + "'");
        }
    }

    actrDirFile.close();

    FragmentStorage fragmentStorage(msg.Actor, msg.FileHash);
    fragmentStorage.insertFragment(msg);

    return newFileHash;
}

bool DfsController::insertDataChunk(std::string data, uint64_t position, std::filesystem::path file) {
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream ofs(tempFilePath.string(), std::ios::binary);
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    uint64_t fz = std::filesystem::file_size(file);
    ofs.write(data.c_str(), data.size()); // add data to new temp file
    ofs.flush();
    std::size_t i = 0;
    for (i = position; i < fz; i = i + DFSB::sectionSize) { // copy old data to new temp file
        if (i + DFSB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFSB::sectionSize);
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
    uint64_t fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DFSB::sectionSize) { // copy new data to old file
        if (i + DFSB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                           DFSB::sectionSize);
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

bool DfsController::removeDataChunk(uint64_t position, uint64_t length, std::filesystem::path file) {
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream ofs(tempFilePath.string());
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    uint64_t fz = std::filesystem::file_size(file);
    std::size_t i = 0;
    for (i = position + length; i < fz; i = i + DFSB::sectionSize) { // copy old data to new temp file
        if (i + DFSB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           DFSB::sectionSize);
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
    uint64_t fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DFSB::sectionSize) { // copy new data to old file
        if (i + DFSB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                           DFSB::sectionSize);
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

DBRow DfsController::makeActrDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                                      uint64_t fileSize) {
    return { { "fileHash", fileHash },
             { "fileHashPrev", fileHashPrev },
             { "filePath", filePath },
             { "fileSize", std::to_string(fileSize) },
             { "lastModified", std::to_string(Utils::currentDateSecs()) } };
}

uint64_t DfsController::calculateSizeTaken(const std::string &folder) {
    uint64_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            size += entry.file_size();
        } else if (entry.is_directory()) {
            size += calculateSizeTaken(entry.path().string());
        }
    }

    return size;
}

std::string DfsController::extractFragment(boost::interprocess::file_mapping &fmapTarget, uint64_t offset,
                                           uint64_t fragmentSize) {
    boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_only, offset,
                                                   fragmentSize);
    char *rr_ptr = static_cast<char *>(rightRegion.get_address());
    return std::string(rr_ptr, fragmentSize);
}

std::string DfsController::extractFragment(boost::interprocess::file_mapping &fmapTarget, uint64_t offset) {
    boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_only, offset);
    char *rr_ptr = static_cast<char *>(rightRegion.get_address());
    return std::string(rr_ptr, rightRegion.get_size());
}

void DfsController::requestSync() {
    node.network()->send_message(Utils::currentDateSecs(), MessageType::DfsLastModified,
                                 MessageStatus::Request);
}

void DfsController::sendSync(uint64_t lastModified, const std::string &messageId) {
    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    auto actors = dirsFile.select("SELECT actorId FROM " + DFST::DirsFile::TableName
                                  + " WHERE lastModified = " + std::to_string(lastModified));
    for (auto &row : actors) {
        sendDirData(row["actorId"], lastModified, messageId);
    }
}

void DfsController::requestDirData(const ActorId &actorId) {
    node.network()->send_message(actorId, MessageType::DfsDirData, MessageStatus::Request);
}

void DfsController::sendDirData(const ActorId &actorId, uint64_t lastModified, const std::string &messageId) {
    if (!std::filesystem::exists(DFST::ActorDirFile::actorDbPath(actorId.toStdString()))) {
        return;
    }
    auto rows = DFST::ActorDirFile::getDirRows(actorId.toStdString(), lastModified);
    if (!rows.empty()) {
        node.network()->send_message(std::pair { actorId, rows }, MessageType::DfsDirData,
                                     MessageStatus::Response, messageId, Config::Net::TypeSend::Focused);
    }
}

void DfsController::addDirData(const ActorId &actorId, const std::vector<DFSP::DirRow> &dirRows) {
    bool res = DFST::ActorDirFile::addDirRows(actorId.toStdString(), dirRows);
    qDebug() << "[Dfs] addDirData result:" << res;

    // temp
    for (auto &row : dirRows) {
        requestFile(actorId, row.fileHash);
    }
}

void DfsController::requestFile(const ActorId &actorId, const std::string &fileHash) {
    std::filesystem::remove(DFS_PATH::filePath(actorId, fileHash));
    node.network()->send_message(std::pair { actorId, fileHash }, MessageType::DfsRequestFile,
                                 MessageStatus::Request);
}

void DfsController::sendFile(const ActorId &actorId, const std::string &fileHash,
                             const std::string &messageId) {
    if (fileHash.empty()) {
        qFatal("[Dfs] Empty file hash");
    }

    auto dirRow = DFST::ActorDirFile::getDirRow(actorId.toStdString(), fileHash);
    DFSP::AddFileMessage msg = { .Actor = actorId.toStdString(),
                                 .FileHash = dirRow.fileHash,
                                 .Path = dirRow.filePath,
                                 .Size = dirRow.fileSize };
    qDebug() << "sendFile" << actorId.toString() << dirRow.fileHash.c_str() << dirRow.filePath.c_str()
             << dirRow.fileSize;

    if (messageId.empty()) {
        node.network()->send_message(msg, MessageType::DfsAddFile);
    } else {
        node.network()->send_message(msg, MessageType::DfsAddFile, MessageStatus::Response, messageId,
                                     Config::Net::TypeSend::Focused);
    }
}

std::string DfsController::sendFragment(const DFSP::RequestFileSegmentMessage &msg,
                                        const std::string &messageId) {
    std::filesystem::path realFilePath = DFS_PATH::filePath(msg.Actor, msg.FileHash);
    if (!std::filesystem::exists(realFilePath)) {
        return "";
        qFatal("[Dfs] No file");
    }

    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string data;
    auto fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize - msg.Offset > DFSB::sectionSize) {
        data = extractFragment(fmapTarget, msg.Offset, DFSB::sectionSize);
    } else {
        data = extractFragment(fmapTarget, msg.Offset);
    }

    DFSP::SegmentMessage fragment = {
        .Actor = msg.Actor, .FileHash = msg.FileHash, .Data = std::move(data), .Offset = msg.Offset
    };

    node.network()->send_message(fragment, MessageType::DfsAddSegment, MessageStatus::Response, messageId,
                                 Config::Net::TypeSend::Focused);
    if (msg.Offset + DFSB::sectionSize >= fileSize) {
        emit uploaded(msg.Actor, msg.FileHash);
        return "";
    }
    emit uploadProgress(msg.Actor, msg.FileHash, double(msg.Offset) / double(fileSize) * 100);
    return "";
}

std::string DfsController::addFragment(const DFSP::SegmentMessage &msg) {
    auto fileName = DFS_PATH::filePath(msg.Actor, msg.FileHash);
    if (!std::filesystem::exists(fileName)) {
        return "";
    }

    DBConnector actrDirFile = DFST::ActorDirFile::actorDbConnector(msg.Actor);
    if (!actrDirFile.isOpen()) {
        qFatal("Error addFragment 1");
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);
    actrDirData = actrDirFile.select("SELECT * FROM " + DFST::ActorDirFile::TableName + " WHERE fileHash = '"
                                     + msg.FileHash + "';");
    std::string virtualPath = actrDirData[0].at("filePath");
    uint64_t fileSize = std::stoull(actrDirData[0].at("fileSize"));

    if (fileSize == std::filesystem::file_size(fileName)) {
        qDebug() << "[Dfs] Skip fragment";
        return "";
    }

    std::string hash = insertFragment(msg);

    uint64_t offset = msg.Offset + DFSB::sectionSize;
    if (fileSize <= offset) {
        if (msg.FileHash == Utils::calcHashForFile(fileName)) {
            qDebug() << "[Dfs] File" << fileName.c_str() << "done";
            // TODO: send package done
            files.erase(msg.Actor + msg.FileHash);
            emit downloaded(msg.Actor, msg.FileHash);
            // node.network()->send_message(std::pair(msg.Actor, msg.FileHash),
            // MessageType::DfsSendingFileDone);
            sendFile(msg.Actor, msg.FileHash, ""); // temp
            return hash;
        } else {
            requestFile(msg.Actor, msg.FileHash);
            qFatal("[Dfs] Incorrect file check");
            return "";
        }
    }

    emit downloadProgress(msg.Actor, msg.FileHash, double(msg.Offset) / double(fileSize) * 100);
    DFSP::RequestFileSegmentMessage reqMessage = {
        .Actor = msg.Actor, .FileHash = msg.FileHash, .Path = virtualPath, .Offset = offset
    };
    node.network()->send_message(reqMessage, MessageType::DfsRequestFileSegment, MessageStatus::Request);

    return hash;
}

std::string DfsController::deleteFragment(const DFSP::DeleteSegmentMessage &msg) {
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::filesystem::path realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByHash(&actrDirFile, msg.FileHash);

    if (actrDirData.empty()) {
        qDebug() << "[Dfs] editFile: Skipped because of empty result";
        qFatal("Error: actrDirData.empty() || localDirData.empty() in delete");
        return "";
    }

    if (actrDirData.size() > 2) {
        qDebug() << "[Dfs] editFile: Query select failed: Query result has unsupported size:"
                 << actrDirData.size();
        qFatal("Error 1");
        return "";
    }
    removeDataChunk(msg.Offset, msg.Size, realFilePath);
    std::string newFileHash = Utils::calcHashForFile(realFilePath.string());
    // uint64_t newFileSize = std::filesystem::file_size(realFilePath);

    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileHash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHash = " +
            // "'"
            //                    + newFileHash + "' " + "WHERE " + "fileHash = " + "'" +
            //                    it->at("fileHash")
            //                    + "'");
        }
        if (it->at("fileHashPrev") == msg.FileHash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHashPrev =
            // " +
            // "'"
            //                    + newFileHash + "' " + "WHERE " + "fileHash = " + "'" +
            //                    it->at("fileHash")
            //                    + "'");
        }
    }

    FragmentStorage fragmentStorage(msg.Actor, msg.FileHash);
    fragmentStorage.removeFragment(msg);

    return newFileHash;
}

uint64_t DfsController::bytesLimit() const {
    return m_bytesLimit;
}

void DfsController::setBytesLimit(uint64_t bytesLimit) {
    m_bytesLimit = bytesLimit;
}

DFSP::AddFileMessage DfsController::getFileHeader(const ActorId actor, const std::string fileHash) {
    DFSP::AddFileMessage ret;
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath =
        DFSB::fsActrRoot + pathDelim + actor.toStdString() + pathDelim + DFSB::fsMapName;
    DBConnector actrDirFile(actrDirFilePath);

    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByHash(&actrDirFile, fileHash);
    actrDirFile.close();
    ret.FileHash = fileHash;
    ret.Actor = actor.toStdString();
    ret.Path = actrDirData.at(0).at("filePath");
    ret.Size = std::stoull(actrDirData.at(0).at("fileSize"));
    return ret;
}
