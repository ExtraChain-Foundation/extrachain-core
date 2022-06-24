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
    qDebug() << fmt::format("[Dfs] Started. Current size: {}, available: {}", m_sizeTaken, bytesAvailable())
                    .c_str();
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

    auto fileSize = std::filesystem::file_size(newFilePath);
    if (!writeAvailable(fileSize)) {
        return "ErrorStorageFull";
    }

    if (securityLevel == DFS::Encryption::Encrypted) {
        std::wstring fname = std::filesystem::path(fpath).stem().wstring();
        newFilePath = L"temp";
        std::filesystem::create_directories(newFilePath);
        newFilePath = newFilePath.wstring() + DFSB::separator + fname;
        if (!std::filesystem::exists(newFilePath)) {
            std::filesystem::copy(filePath, newFilePath);
        }
        actor.key().encryptFile(fpath, newFilePath);

        std::filesystem::path nvp = targetVirtualFilePath;
        std::filesystem::path nfn = nvp.filename();
        nvp.remove_filename();
        nvp /= "secured";
        nvp /= nfn;
        newTargetVirtualFilePath = nvp.string();
    }

    std::string fileName = createFileName(filePath);
    std::string fileHash = Utils::calcHashForFile(newFilePath);
    std::filesystem::path placeInDFS =
        DFSB::fsActrRootW + DFSB::separator + actor.id().toString().toStdWString() + DFSB::separator;
    std::filesystem::path dfsPath = DFS_PATH::filePath(actor.id(), fileName);

    if (std::filesystem::exists(dfsPath) && std::filesystem::file_size(dfsPath) == fileSize) {
        std::string dfsFileHash = Utils::calcHashForFile(dfsPath);
        if (fileHash == dfsFileHash) {
            qDebug() << "[DFS] File already in DFS";
            return "ErrorAlreadyExists";
        }
    }

    try {
        std::filesystem::create_directories(placeInDFS.c_str());
#ifdef ANDROID
        std::filesystem::rename(newFilePath, dfsPath);
#else
        std::filesystem::copy(newFilePath, dfsPath);
#endif
    } catch (std::filesystem::filesystem_error const &err) {
        qDebug() << "[Dfs] Copy error:" << err.what();
    }

    if (std::filesystem::exists(newFilePath) && securityLevel == DFS::Encryption::Encrypted)
        std::filesystem::remove(newFilePath);

    FragmentStorage fs(actor.id(), fileName, fileHash);
    fs.initLocalFile(fileSize);

    const auto actorId = actor.id().toStdString();
    DFSP::AddFileMessage msg = { .Actor = actorId,
                                 .FileName = fileName,
                                 .FileHash = fileHash,
                                 .Path = newTargetVirtualFilePath,
                                 .Size = fileSize };
    qDebug() << "AddFileMessage" << actor.id().toString() << fileName.c_str()
             << newTargetVirtualFilePath.c_str() << fileSize;

    auto actrDirFile = DFST::ActorDirFile::actorDbConnector(actorId);
    auto lastFileName = DFST::ActorDirFile::getLastName(actrDirFile);
    const DBRow rowData = makeActrDirDBRow(msg.FileName, lastFileName, msg.FileHash, msg.Path, msg.Size);

    if (!actrDirFile.insert(DFST::ActorDirFile::TableName, rowData)) {
        qDebug() << "[Dfs] addFile: insert failed:" << actrDirFile.file().c_str() << " :"
                 << DFST::ActorDirFile::TableName.c_str();
        qFatal("Insert failed");
        return "ErrorDirError";
    }

    actrDirFile.close();
    m_sizeTaken += fileSize;
    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    dirsFile.replace(DFST::DirsFile::TableName,
                     { { "actorId", actorId }, { "lastModified", rowData.at("lastModified") } });

    sendFile(actor.id(), fileName);

//    HistoricalChain hc((DFS_PATH::filePath(actorId, fileName).string() + DFSF::Extension), fpath.string());
//    hc.initLocal(actorId, fileName, fileHash);

    return addFile(msg, false);
}

bool DfsController::removeLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath) {
    std::string fileHash = Utils::calcHashForFile(filePath); // TODO: get hash
    const auto actorId = actor.id().toStdString();
    DFSP::RemoveFileMessage msg = { .Actor = actorId,
                                    .FileName = std::filesystem::path(filePath).filename().string() };
    node.network()->send_message(msg, MessageType::DfsRemoveFile);

    HistoricalChain hc((DFS_PATH::filePath(actorId, fileHash).string() + DFSF::Extension), filePath);
    const bool databaseFileRemoved = hc.remove(actorId, fileHash);
    const bool fileRemoved = std::filesystem::remove(DFS_PATH::filePath(actorId, fileHash).string());
    return databaseFileRemoved && fileRemoved;
}

std::string DfsController::addFile(const DFSP::AddFileMessage &msg, bool loadBytes) {
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::string realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileName;

    if (loadBytes) {
        if (std::filesystem::exists(realFilePath)) {
            qDebug() << "[Dfs] File already exists"; // temp: not correct, add calc file
            return msg.FileHash;
        }
        if (!writeAvailable(msg.Size)) {
            qDebug() << "[Dfs] Storage full";
            qFatal("[Dfs] Storage full");
            return msg.FileName;
        }
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
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("fileName") : "";

    const DBRow rowData = makeActrDirDBRow(msg.FileName, lastFileName, msg.FileHash, msg.Path, msg.Size);

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
            return msg.FileName;
        } else {
            DFSP::RequestFileSegmentMessage reqMessage = { .Actor = msg.Actor,
                                                           .FileName = msg.FileName,
                                                           .FileHash = msg.FileHash,
                                                           .Path = msg.Path,
                                                           .Offset = 0 };
            node.network()->send_message(reqMessage, MessageType::DfsRequestFileSegment,
                                         MessageStatus::Request);
        }
    }

    files[msg.Actor + msg.FileName] = msg;
    emit added(msg.Actor, msg.FileName, msg.Path, msg.Size);

    return msg.FileName;
}

std::string DfsController::getFileFromStorage(ActorId owner, std::string fileName) {
    Actor<KeyPrivate> localOwner = node.accountController()->currentProfile().getActor(owner);
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path realFilePath =
        DFSB::fsActrRoot + pathDelim + owner.toStdString() + pathDelim + fileName;
    std::string actrDirFilePath =
        DFSB::fsActrRoot + pathDelim + owner.toStdString() + pathDelim + DFSB::fsMapName;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        qFatal("Can't open %s", actrDirFilePath.c_str());
        exit(EXIT_FAILURE);
    }

    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByName(&actrDirFile, fileName);
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
    qDebug() << "[Dfs] Remove file message:" << msg.FileName.c_str();
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::filesystem::path realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileName;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByName(&actrDirFile, msg.FileName);
    std::string prevHash;
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.FileName) {
            prevHash = it->at("fileHashPrev");
            actrDirFile.deleteRow(DFST::ActorDirFile::TableName, *it);
            if (!std::filesystem::remove(realFilePath)) {
                qDebug() << "File removal by path " << realFilePath.c_str() << " failed";
                return false;
            }
        }
        if ((it->at("fileHashPrev") == msg.FileName) && (!prevHash.empty())) {
            actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHashPrev = " + "'"
                               + prevHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }

    actrDirFile.close();

    return true;
}

std::string DfsController::createFileName(std::filesystem::path file) {
    int64_t time = std::chrono::system_clock::now().time_since_epoch().count();
    std::string filename = file.filename().string();
    boost::mt11213b rng(time);
    boost::random::uniform_int_distribution<> dist(0, INT_MAX);
    std::string salt = Tools::typeToStdStringBytes<int>(dist(rng));
    std::string ret = Utils::calcHash(filename + std::to_string(time) + salt);
    return ret;
}

bool DfsController::renameFile(const ActorId &actor, const std::string &fileHash,
                               const std::string &newFileHash) {
    const std::string actorId = actor.toStdString();
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::path path = DFSB::fsActrRoot + pathDelim + actorId + pathDelim;
    std::filesystem::rename(path / std::string(fileHash), path / std::string(newFileHash));
    return std::filesystem::exists(path / std::string(newFileHash));
}

std::string DfsController::insertFragment(const DFSP::SegmentMessage &msg) {
    qDebug() << "[Dfs] Edit file:" << msg.FileHash.c_str();
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::filesystem::path realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileName;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByName(&actrDirFile, msg.FileName);

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
    actrDirFile.close();
    return Utils::calcHashForFile(realFilePath.string());
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

DBRow DfsController::makeActrDirDBRow(std::string fileName, std::string fileNamePrev, std::string fileHash,
                                      std::string filePath, uint64_t fileSize) {
    return { { "fileName", fileName },
             { "fileNamePrev", fileNamePrev },
             { "fileHash", fileHash },
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

void DfsController::requestFile(const ActorId &actorId, const std::string &fileName) {
    std::filesystem::remove(DFS_PATH::filePath(actorId, fileName));
    node.network()->send_message(std::pair { actorId, fileName }, MessageType::DfsRequestFile,
                                 MessageStatus::Request);
}

void DfsController::sendFile(const ActorId &actorId, const std::string &fileName,
                             const std::string &messageId) {
    if (fileName.empty()) {
        qFatal("[Dfs] Empty file name");
    }

    auto dirRow = DFST::ActorDirFile::getDirRow(actorId.toStdString(), fileName);
    DFSP::AddFileMessage msg = { .Actor = actorId.toStdString(),
                                 .FileName = fileName,
                                 .FileHash = dirRow.fileHash,
                                 .Path = dirRow.filePath,
                                 .Size = dirRow.fileSize };

    if (messageId.empty()) {
        node.network()->send_message(msg, MessageType::DfsAddFile);
    } else {
        node.network()->send_message(msg, MessageType::DfsAddFile, MessageStatus::Response, messageId,
                                     Config::Net::TypeSend::Focused);
    }
}

std::string DfsController::sendFragment(const DFSP::RequestFileSegmentMessage &msg,
                                        const std::string &messageId) {
    std::filesystem::path realFilePath = DFS_PATH::filePath(msg.Actor, msg.FileName);
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

    DFSP::SegmentMessage fragment = { .Actor = msg.Actor,
                                      .FileName = msg.FileName,
                                      .FileHash = msg.FileHash,
                                      .Data = std::move(data),
                                      .Offset = msg.Offset };

    node.network()->send_message(fragment, MessageType::DfsAddSegment, MessageStatus::Response, messageId,
                                 Config::Net::TypeSend::Focused);
    if (msg.Offset + DFSB::sectionSize >= fileSize) {
        emit uploaded(msg.Actor, msg.FileName);
        return "";
    }
    emit uploadProgress(msg.Actor, msg.FileName, double(msg.Offset) / double(fileSize) * 100);
    return "";
}

std::string DfsController::addFragment(const DFSP::SegmentMessage &msg) {
    auto fileName = DFS_PATH::filePath(msg.Actor, msg.FileName);
    if (!std::filesystem::exists(fileName)) {
        return "";
    }

    DBConnector actrDirFile = DFST::ActorDirFile::actorDbConnector(msg.Actor);
    if (!actrDirFile.isOpen()) {
        qFatal("Error addFragment 1");
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByName(&actrDirFile, msg.FileName);
    actrDirData = actrDirFile.select("SELECT * FROM " + DFST::ActorDirFile::TableName + " WHERE fileName = '"
                                     + msg.FileName + "';");
    std::string virtualPath = actrDirData[0].at("filePath");
    uint64_t fileSize = std::stoull(actrDirData[0].at("fileSize"));

    if (fileSize == std::filesystem::file_size(fileName)) {
        qDebug() << "[Dfs] Skip fragment";
        return "";
    }

    FragmentStorage fs(msg);
    fs.insertFragment(msg);

    uint64_t offset = msg.Offset + DFSB::sectionSize;
    if (fileSize <= offset) {
        // To do: fix compare by hash
        if (msg.FileHash == Utils::calcHashForFile(fileName)) {
            qDebug() << "[Dfs] File" << fileName.c_str() << "done";
            // TODO: send package done
            files.erase(msg.Actor + msg.FileName);
            emit downloaded(msg.Actor, msg.FileName);
            sendFile(msg.Actor, msg.FileName); // temp
            return "hash";
        } else {
            requestFile(msg.Actor, msg.FileName);
            qFatal("[Dfs] Incorrect file check");
            return "";
        }
    }

    emit downloadProgress(msg.Actor, msg.FileName, double(msg.Offset) / double(fileSize) * 100);

    DFSP::RequestFileSegmentMessage reqMessage = { .Actor = msg.Actor,
                                                   .FileName = msg.FileName,
                                                   .FileHash = msg.FileHash,
                                                   .Path = virtualPath,
                                                   .Offset = offset };
    node.network()->send_message(reqMessage, MessageType::DfsRequestFileSegment, MessageStatus::Request);

    return "";
}

std::string DfsController::deleteFragment(const DFSP::DeleteSegmentMessage &msg) {
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::filesystem::path realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileHash;
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByName(&actrDirFile, msg.FileName);

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

    FragmentStorage fragmentStorage(msg.Actor, msg.FileName, msg.FileHash);
    fragmentStorage.removeFragment(msg);

    return newFileHash;
}

uint64_t DfsController::bytesLimit() const {
    return m_bytesLimit;
}

void DfsController::setBytesLimit(uint64_t bytesLimit) {
    m_bytesLimit = bytesLimit;
    qDebug() << "[Dfs] Changed limit:" << m_bytesLimit;
}

DFSP::AddFileMessage DfsController::getFileHeader(const ActorId actor, const std::string fileName) {
    DFSP::AddFileMessage ret;
    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath =
        DFSB::fsActrRoot + pathDelim + actor.toStdString() + pathDelim + DFSB::fsMapName;
    DBConnector actrDirFile(actrDirFilePath);

    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFST::ActorDirFile::getFileDataByName(&actrDirFile, fileName);
    actrDirFile.close();
    ret.FileHash = actrDirData.at(0).at("fileHash");
    ret.FileName = fileName;
    ret.Actor = actor.toStdString();
    ret.Path = actrDirData.at(0).at("filePath");
    ret.Size = std::stoull(actrDirData.at(0).at("fileSize"));
    return ret;
}

uint64_t DfsController::bytesAvailable() {
    auto freeDfs = m_bytesLimit <= m_sizeTaken ? 0 : m_bytesLimit - m_sizeTaken;
    uint64_t freeDisk = Utils::diskFreeMemory();
    auto min = m_bytesLimit == 0 ? freeDisk : std::min(freeDfs, freeDisk);
    return min;
}

bool DfsController::writeAvailable(uint64_t size) {
    return bytesAvailable() > size + 10000;
}
