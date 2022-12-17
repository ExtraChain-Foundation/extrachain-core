#include "datastorage/dfs/dfs_controller.h"

DfsController::DfsController(ExtraChainNode &node, QObject *parent)
    : QObject(parent)
    , node(node) {
    std::filesystem::create_directories(DFSB::fsActrRoot);

    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    dirsFile.query(DFST::DirsFile::CreateTableQuery);

    m_sizeTaken = calculateSizeTaken();
    m_totalDfsSize = calculateFilesSize();
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
    m_totalDfsSize += fileSize;
    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    dirsFile.replace(DFST::DirsFile::TableName,
                     { { "actorId", actorId }, { "lastModified", rowData.at("lastModified") } });

    sendFile(actor.id(), fileName);
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

void DfsController::addListFiles(const QStringList &files) {
    qDebug() << "Files add in thread id: [" << QThread::currentThreadId() << "]" << files.size();
    const auto actor = node.accountController()->mainActor();
    ThreadAddFiles addFilesThread(this, actor, files);
    connect(&addFilesThread, &ThreadAddFiles::added, this,
            [&](DFSP::AddFileMessage msg, std::string filePath, std::string scriptPath) {
                insertToFiles(msg);
                emit added(msg.Actor, msg.FileName, msg.Path, msg.Size);
                emit resultAddFile("", QString::fromStdString(filePath));
                if (!scriptPath.empty()) {
                    //                    Transaction transaction;
                    //                    transaction.setData(MessagePack::serialize(
                    //                        TransactionData { .hash = transaction.getHash(), .path =
                    //                        msg.FileHash }));
                    //                    node.network()->send_message(transaction,
                    //                    MessageType::BlockchainTransaction);
                }
            });

    connect(&addFilesThread, &ThreadAddFiles::sendMessage, this,
            [&](DFSP::AddFileMessage msg, MessageType messageType) {
                qDebug() << "send file: " << msg.FileName.c_str();
                node.network()->send_message(msg, MessageType::DfsAddFile);
            });
    connect(&addFilesThread, &ThreadAddFiles::error, this, [&](std::string error, std::string fileName) {
        qDebug() << error.c_str();
        emit resultAddFile(QString::fromStdString(error), QString::fromStdString(fileName));
    });
    addFilesThread.start();
    addFilesThread.wait();
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

uint64_t DfsController::sizeTaken() const {
    return m_sizeTaken;
}

uint64_t DfsController::totalDfsSize() const {
    return m_totalDfsSize;
}

void DfsController::increaseSizeTaken(uintmax_t value) {
    m_sizeTaken += value;
}

void DfsController::insertToFiles(DFS::Packets::AddFileMessage msg) {
    files[msg.Actor + msg.FileName] = msg;
}

void DfsController::exportFile(const std::string &pathTo, const std::string &pathFrom,
                               const std::string &nameFile) {
    std::string actorId = "";

    if (!std::filesystem::exists(pathTo)) {
        std::filesystem::create_directories(pathTo);
    }

    if (pathFrom.find('/') != std::string::npos) {
        size_t pos = pathFrom.rfind('/');
        actorId = pathFrom.substr(pos + 1, pathFrom.size());
    } else {
        actorId = pathFrom;
        std::filesystem::path actorFolderPath = DFSB::fsActrRoot + "/" + actorId;
        exportFile(pathTo, actorFolderPath.string(), nameFile);
    }

    if (actorId.empty()) {
        qDebug() << "Path or actor_id hadn't been found. Please check in parameters.";
        return;
    }

    if (!nameFile.empty()) {
        std::string pathFile = pathFrom + "/" + nameFile;
        const bool fileFromExist = std::filesystem::exists(pathFile);
        const bool folderToExist = std::filesystem::exists(pathTo);
        if (fileFromExist && folderToExist) {
            std::filesystem::copy(pathFile, pathTo);
            auto dirRows = DFS::Tables::ActorDirFile::getDirRows(actorId);
            auto it = std::find_if(dirRows.begin(), dirRows.end(), [&](DFSP::DirRow &dbRow) {
                transform(dbRow.fileName.begin(), dbRow.fileName.end(), dbRow.fileName.begin(), ::tolower);
                auto lowerNameFile = nameFile;
                transform(lowerNameFile.begin(), lowerNameFile.end(), lowerNameFile.begin(), ::tolower);
                if (dbRow.fileName == lowerNameFile) {
                    if (!std::filesystem::exists(pathTo + "/" + dbRow.filePath)) {
                        std::filesystem::rename(pathTo + "/" + nameFile, pathTo + "/" + dbRow.filePath);
                    } else {
                        const auto pathFile = std::filesystem::path(pathTo + "/" + dbRow.filePath);
                        for (int index = 2; index < 100; index++) {
                            std::string possibleNewFile = pathTo + "/" + pathFile.stem().string() + "_"
                                + std::to_string(index) + pathFile.extension().string();
                            if (!std::filesystem::exists(possibleNewFile)) {
                                std::filesystem::rename(pathTo + "/" + nameFile, possibleNewFile);
                                break;
                            }
                        }
                    }
                    qDebug() << fmt::format("File \"{}\" of actor \"{}\" extracted\n", dbRow.filePath,
                                            actorId)
                                    .c_str();
                    return true;
                }
                return false;
            });
        }
    } else {
        const std::string nameDirectory = pathTo + "/" + actorId;
        std::filesystem::create_directories(nameDirectory);
        if (pathFrom.find('/') != std::string::npos) {
            for (std::filesystem::directory_entry const &entry :
                 std::filesystem::directory_iterator(pathFrom)) {
                if (entry.path().extension() != ".storj" && entry.path().extension() != ".storj-journal"
                    && entry.path().filename() != ".dir") {
                    auto copyTo = (pathTo + "/" + actorId);
                    exportFile(copyTo, pathFrom, entry.path().filename().string());
                }
            }
        }
    }
}

uint64_t DfsController::calculateSizeTaken(const std::string &folder) const {
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

uint64_t DfsController::calculateFilesSize(const std::string &folder) const {
    uint64_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().filename() == DFS::Basic::fsMapName) {
            const std::string actorId = entry.path().parent_path().filename().string();
            size += DFST::ActorDirFile::totalFileSize(actorId);
        } else if (entry.is_directory()) {
            size += calculateFilesSize(entry.path().string());
        }
    }

    return size;
}

uint64_t DfsController::calculateDataAmountStored(const std::string &folder) const {
    uint64_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == DFSF::Extension) {
            const std::string actorId = entry.path().parent_path().filename().string();
            size += DFST::ActorDirFile::dataAmountStoredSize(actorId, entry.path().filename().string());
        } else if (entry.is_directory()) {
            size += calculateDataAmountStored(entry.path().string());
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

void DfsController::sendSizeRequestMsg(const ActorId &actorId) const {
    DFSP::RequestDfsSize msg { actorId.toStdString() };
    node.network()->send_message(msg, MessageType::RequestDfsSize, MessageStatus::Request);
}

void DfsController::sendSizeReponseMsg(const DFS::Packets::RequestDfsSize &msg,
                                       const std::string &messageId) const {
    const auto dfsSize = calculateSizeTaken();
    DFSP::ResponseDfsSize response { .Actor = msg.Actor, .Size = dfsSize };
    node.network()->send_message(response, MessageType::ResponseDfsSize, MessageStatus::Response, messageId);
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
    qDebug() << fileName.c_str();
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

void DfsController::fetchFragments(DFS::Packets::RequestFileSegmentMessage &msg, std::string &messageId) {
    std::filesystem::path realFilePath = DFS_PATH::filePath(msg.Actor, msg.FileName);
    if (!std::filesystem::exists(realFilePath)) {
        return;
    }

    auto fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize == 0) {
        return;
    }
    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string data;
    uint64_t totalOffset = 0;
    bool lastFragment = false;
    do {
        uint64_t limitSectionSize = 0;
        while (limitSectionSize <= DFSB::maxSectionSize && !lastFragment) {
            if (fileSize - totalOffset > DFSB::sectionSize) {
                data += extractFragment(fmapTarget, totalOffset, DFSB::sectionSize);
                totalOffset += DFSB::sectionSize;
                limitSectionSize += DFSB::sectionSize;
                qDebug() << "progress: [" << (double(totalOffset) / double(fileSize) * 100) << "%]";
                emit uploadProgress(msg.Actor, msg.FileName, double(totalOffset) / double(fileSize) * 100);
            } else {
                lastFragment = true;
                data += extractFragment(fmapTarget, totalOffset);
            }
        }

        DFSP::SegmentMessage fragment = { .Actor = msg.Actor,
                                          .FileName = msg.FileName,
                                          .FileHash = msg.FileHash,
                                          .Data = std::move(data),
                                          .Offset = totalOffset };

        messageId =
            node.network()->send_message(fragment, MessageType::DfsAddSegment, MessageStatus::Response,
                                         messageId, Config::Net::TypeSend::Focused);

        if (lastFragment) {
            emit uploaded(msg.Actor, msg.FileName);
            if (std::filesystem::exists(Scripts::folder + "/" + msg.FileName)) {
                node.network()->send_message(fragment, MessageType::BlockchainCopyScript);
            }
        } else {
            emit uploadProgress(msg.Actor, msg.FileName, double(totalOffset) / double(fileSize) * 100);
        }
    } while (!lastFragment);
}

void DfsController::verifyFiles(std::vector<DFS::Packets::VerifyFileMessage> &fileList,
                                std::string &messageId) {
    for (auto &file : fileList) {
        // check file exist
        std::filesystem::path realFilePath = DFS_PATH::filePath(file.Actor, file.FileName);
        if (!std::filesystem::exists(realFilePath)) {
            qDebug() << "File by path" << realFilePath.c_str() << "doesn't exist.";
            continue;
        }
        std::string fileHash = Utils::calcHashForFile(realFilePath);
        if (fileHash == file.FileHash) {
            file.Verified = true;
        }
    }
    std::vector<std::string> serializedData = MessagePack::serializeContainer(fileList);
    node.network()->send_message(serializedData, MessageType::DfsVerifyList, MessageStatus::Response,
                                 messageId, Config::Net::TypeSend::Focused);
}

float DfsController::percentVerified(std::vector<DFS::Packets::VerifyFileMessage> &fileList) {
    float result = 0.0;
    int countFilesVerified = 0;
    for (const auto &msg : fileList) {
        if (msg.Verified) {
            countFilesVerified++;
        }
    }
    result = ((float)countFilesVerified / (float)fileList.size()) * 100;
    return result;
}

std::string DfsController::addFragment(const DFSP::SegmentMessage &msg) {
    auto fileName = DFS_PATH::filePath(msg.Actor, msg.FileName);
    if (!std::filesystem::exists(fileName)
        || std::find(m_compliteFiles.begin(), m_compliteFiles.end(), msg.FileName) != m_compliteFiles.end()) {
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
    auto currentFileSize = std::filesystem::file_size(fileName);
    if (fileSize == currentFileSize) {
        m_compliteFiles.push_back(msg.FileName);
        qDebug() << "[Dfs] File is complite";
        return "";
    }

    FragmentStorage fs(msg);
    fs.insertFragment(msg);
    currentFileSize = std::filesystem::file_size(fileName);
    //    emit downloadProgress(msg.Actor, msg.FileName, double(msg.Offset) / double(fileSize) * 100);
    if (fileSize == currentFileSize) {
        if (msg.FileHash == Utils::calcHashForFile(fileName)) {
            qDebug() << "[Dfs] File" << fileName.c_str() << "done";
            files.erase(msg.Actor + msg.FileName);
            emit downloaded(msg.Actor, msg.FileName);
            sendFile(msg.Actor, msg.FileName); // temp
            fs.initHistoricalChain();
            return "hash";
        } else {
            requestFile(msg.Actor, msg.FileName);
            qFatal("[Dfs] Incorrect file check");
            return "";
        }
    }
    return "";
}

void DfsController::threadAddFragment(const DFS::Packets::SegmentMessage &msg) {
    qDebug() << "add segment. Thread: [" << QThread::currentThreadId() << "]";
    FragmentWriter fw(msg, m_compliteFiles);

    connect(&fw, &FragmentWriter::downloadProgress, this, &DfsController::downloadProgress);
    connect(&fw, &FragmentWriter::eraseFromFiles, this,
            [=](DFSP::SegmentMessage msg) { files.erase(msg.Actor + msg.FileName); });
    connect(&fw, &FragmentWriter::requestFile, this, &DfsController::requestFile);
    connect(&fw, &FragmentWriter::sendFile, this,
            [&](const std::string &actorId, const std::string &fileName) { sendFile(actorId, fileName); });
    connect(&fw, &FragmentWriter::downloadedFile, this, &DfsController::downloaded);
    connect(&fw, &FragmentWriter::compliteFile, this,
            [&](const std::string &fileName) { m_compliteFiles.push_back(fileName); });

    fw.start();
    fw.wait();
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

ThreadAddFiles::ThreadAddFiles(DfsController *dfsController, const Actor<KeyPrivate> &actor,
                               const QStringList &files, QObject *parent)
    : QThread(parent)
    , m_dfsController(dfsController)
    , m_actor(actor)
    , m_files(files) {
    connect(this, &ThreadAddFiles::finished, this, &ThreadAddFiles::deleteLater);
}

ThreadAddFiles::~ThreadAddFiles() {
    qDebug() << "run destructor for thread add files";
}

void ThreadAddFiles::run() {
    qDebug() << "Run function in thread: " << QThread::currentThreadId();
    for (const auto &fileName : m_files) {
        addFile(m_actor, fileName.toStdWString(), QFileInfo(fileName).fileName().toStdString());
    }
}

void ThreadAddFiles::addFile(const Actor<KeyPrivate> &actor, const std::filesystem::path &filePath,
                             std::string targetVirtualFilePath) {
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
        emit error("ErrorNotExists", filePath.string());
        return;
    }

    if (!std::filesystem::is_regular_file(newFilePath)) {
        qInfo() << "[Dfs] This is not a file";
        emit error("ErrorNotFile", filePath.string());
        return;
    }

    std::ifstream my_file(newFilePath);
    if (!my_file) {
        qDebug() << "Can't read";
        emit error("ErrorNotReadable", filePath.string());
        return;
    }
    my_file.close();

    auto fileSize = std::filesystem::file_size(newFilePath);
    if (!m_dfsController->writeAvailable(fileSize)) {
        emit error("ErrorStorageFull", filePath.string());
        return;
    }

    std::string fileName = m_dfsController->createFileName(filePath);
    std::string fileHash = Utils::calcHashForFile(newFilePath);
    std::filesystem::path placeInDFS =
        DFSB::fsActrRootW + DFSB::separator + actor.id().toString().toStdWString() + DFSB::separator;
    std::filesystem::path dfsPath = DFS_PATH::filePath(actor.id(), fileName);

    if (std::filesystem::exists(dfsPath) && std::filesystem::file_size(dfsPath) == fileSize) {
        std::string dfsFileHash = Utils::calcHashForFile(dfsPath);
        if (fileHash == dfsFileHash) {
            qDebug() << "[DFS] File already in DFS";
            emit error("ErrorAlreadyExists", filePath.string());
            return;
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

    const auto actorId = actor.id().toStdString();
    DFSP::AddFileMessage msg = { .Actor = actorId,
                                 .FileName = fileName,
                                 .FileHash = fileHash,
                                 .Path = newTargetVirtualFilePath,
                                 .Size = fileSize };

    auto actrDirFile = DFST::ActorDirFile::actorDbConnector(actorId);
    auto lastFileName = DFST::ActorDirFile::getLastName(actrDirFile);
    const DBRow rowData =
        m_dfsController->makeActrDirDBRow(msg.FileName, lastFileName, msg.FileHash, msg.Path, msg.Size);

    if (!actrDirFile.insert(DFST::ActorDirFile::TableName, rowData)) {
        qDebug() << "[Dfs] addFile: insert failed:" << actrDirFile.file().c_str() << " :"
                 << DFST::ActorDirFile::TableName.c_str();
        qFatal("Insert failed");
        emit error("ErrorDirError", filePath.string());
        return;
    }

    actrDirFile.close();
    m_dfsController->increaseSizeTaken(fileSize);
    DBConnector dirsFile(DFSB::dirsPath);
    dirsFile.open();
    dirsFile.replace(DFST::DirsFile::TableName,
                     { { "actorId", actorId }, { "lastModified", rowData.at("lastModified") } });
    dirsFile.close();
    auto dirRow = DFST::ActorDirFile::getDirRow(actorId, fileName);

    emit sendMessage(msg, MessageType::DfsAddFile);

    std::string pathDelim = Utils::platformDelimeter();
    std::string actrDirFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + DFSB::fsMapName;
    std::string realFilePath = DFSB::fsActrRoot + pathDelim + msg.Actor + pathDelim + msg.FileName;

    DBConnector actrDirPathFile(actrDirFilePath);

    if (!actrDirPathFile.open()) {
        exit(EXIT_FAILURE);
    }

    auto result = actrDirPathFile.select(DFST::filesTableLast);

    if (!actrDirPathFile.insert(DFST::ActorDirFile::TableName, rowData)) {
        qDebug() << "[Dfs] addFile: insert failed:" << actrDirPathFile.file().c_str() << " :"
                 << DFST::ActorDirFile::TableName.c_str();
        qFatal("Error 2");
        return;
    }
    actrDirPathFile.close();
    dirsFile.open();
    dirsFile.replace(DFST::DirsFile::TableName,
                     { { "actorId", msg.Actor }, { "lastModified", rowData.at("lastModified") } });

    FragmentStorage fs(actor.id(), fileName, fileHash);
    fs.initLocalFile(fileSize);
    fs.initHistoricalChain();

    //    const bool isScript = filePath.extension() == Scripts::wasmExtention;
    //    std::string scriptPath = "";
    //    if (isScript) {
    //        scriptPath = Scripts::folder + "/" + fileName;
    //        std::filesystem::copy(filePath, scriptPath);
    //    };
    emit added(msg, filePath.string());
}
