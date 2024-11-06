#include "datastorage/dfs/dfs_controller.h"
#include "datastorage/dfs/fragment_storage.h"

DfsController::DfsController(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
    std::filesystem::create_directories(DfsB::fsActrRoot);

    DBConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    dirsFile.query(DfsT::DirsFile::CreateTableQuery);
    dirsFile.query(DfsT::DirsFile::CreateParametersTableQuery);
    dirsFile.close();

    m_sizeTaken    = calculateSizeTaken();
    m_totalDfsSize = calculateFilesSize();
    loadBytesLimit();
    qDebug() << fmt::format("[Dfs] Started. Current size: {}, available: {}", m_sizeTaken, bytesAvailable())
                    .c_str();

    if (!node->accountController()->empty())
        requestDirFileAllActors();
}

DfsController::~DfsController() {
    qInfo("DfsController::~DfsController()");
}

void DfsController::initializeActor(const ActorId &actorId) {
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::create_directories(DfsB::fsActrRoot + pathDelim + actorId.toString());
    DBConnector actrDirFile = DfsT::ActorDirFile::actorDbConnector(actorId);
    actrDirFile.query(DfsT::ActorDirFile::CreateTableQuery);
    requestDirData(actorId);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::storeFile(
    const ActorId               &actorId,
    const std::filesystem::path &filePath,
    const std::string           &visualFolder,
    const std::string           &visualName,
    Dfs::Encryption              securityLevel) {
    std::filesystem::path fpath                    = DfsPath::convertPathToPlatform(filePath);
    std::filesystem::path newFilePath              = fpath;
    std::string           newTargetVirtualFilePath = visualFolder + "/" + visualName;

#ifdef ANDROID
    auto tempPath =
        "dfs/temp"
        + QString::number(QRandomGenerator::global()->bounded(1000) + QDateTime::currentMSecsSinceEpoch());
    QFile::copy(newFilePath.string().c_str(), tempPath);
    fpath       = tempPath.toStdString();
    newFilePath = fpath;
#endif

    if (!std::filesystem::exists(newFilePath)) {
        qInfo() << "[Dfs] Can't load file";
        return std::unexpected(Dfs::DfsError::NotExists);
    }

    if (!std::filesystem::is_regular_file(newFilePath)) {
        qInfo() << "[Dfs] This is not a file";
        return std::unexpected(Dfs::DfsError::NotFile);
    }

    std::ifstream my_file(newFilePath);
    if (!my_file) {
        qDebug() << "Can't read";
        return std::unexpected(Dfs::DfsError::NotReadable);
    }
    my_file.close();

    auto fileSize = std::filesystem::file_size(newFilePath);
    if (!writeAvailable(fileSize)) {
        return std::unexpected(Dfs::DfsError::StorageFull);
    }

    if (securityLevel == Dfs::Encryption::Encrypted) {
        std::wstring fname = std::filesystem::path(fpath).stem().wstring();
        newFilePath        = L"temp";
        std::filesystem::create_directories(newFilePath);
        newFilePath = newFilePath.wstring() + DfsB::separator + fname;
        if (!std::filesystem::exists(newFilePath)) {
            std::filesystem::copy(filePath, newFilePath);
        }

        auto actor = node->accountController()->currentProfile().getActor(actorId);
        actor->key().encryptFile(fpath, newFilePath);

        std::filesystem::path nvp = newTargetVirtualFilePath;
        std::filesystem::path nfn = nvp.filename();
        nvp.remove_filename();
        nvp /= "secured";
        nvp /= nfn;
        newTargetVirtualFilePath = nvp.string();
    }

    std::string           fileId   = createFileId(filePath);
    std::string           fileHash = Utils::calcHashForFile(newFilePath);
    std::filesystem::path placeInDFS =
        DfsB::fsActrRootW + DfsB::separator + actorId.toQString().toStdWString() + DfsB::separator;
    std::filesystem::path dfsPath = DfsPath::filePath(actorId, fileId);

    if (std::filesystem::exists(dfsPath) && std::filesystem::file_size(dfsPath) == fileSize) {
        std::string dfsFileHash = Utils::calcHashForFile(dfsPath);
        if (fileHash == dfsFileHash) {
            qDebug() << "[DFS] File already in DFS";
            return std::unexpected(Dfs::DfsError::AlreadyExists);
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
        qDebug() << "[Dfs] Copy error:" << err.what(); // error
    }

    if (std::filesystem::exists(newFilePath) && securityLevel == Dfs::Encryption::Encrypted)
        std::filesystem::remove(newFilePath);

    // create new dir row
    Dfs::DirRow dirRow = { .actorId      = actorId,
                           .fileId       = fileId,
                           .fileIdPrev   = "",
                           .hash         = fileHash,
                           .folder       = visualFolder,
                           .name         = visualName,
                           .size         = fileSize,
                           .created      = 0,
                           .lastModified = 0,
                           .type         = Dfs::FileType::Bytes,
                           .encryption   = securityLevel,
                           .state        = Dfs::FileState::Loaded };

    auto res = Dfs::Tables::ActorDirFile::addDirRow(actorId, dirRow);
    if (!res) {
        // TODO: remove file?
        return std::unexpected(Dfs::DfsError::DirError);
    }

    increaseSizeTaken(fileSize);
    m_totalDfsSize += fileSize; // TODO: is need at this place?

    FragmentStorage fs(actorId, fileId, fileHash);
    fs.initLocalFile(fileSize);
    fs.initHistoricalChain();

    updateDirsLastModified(actorId, dirRow.lastModified);

    insertToFiles(dirRow);
    emit added(dirRow);
    sendFile(actorId, fileId);

    return dirRow;
    // return addFile(msg, false);
}

std::expected<Dfs::DirRow, Dfs::DfsError>
DfsController::storeDb(const ActorId &actorId, const std::string &visualName, const DbSchema &schema) {
    std::string           fileId  = createFileIdFromData("db");
    std::filesystem::path dfsPath = DfsPath::filePath(actorId, fileId);
    DBConnector           db(dfsPath);
    db.open();
    db.createTable(schema);
    db.close();

    std::string fileHash = Utils::calcHashForFile(dfsPath);
    auto        fileSize = std::filesystem::file_size(dfsPath);

    Dfs::DirRow dirRow = { .actorId      = actorId,
                           .fileId       = fileId,
                           .fileIdPrev   = "",
                           .hash         = fileHash,
                           .folder       = "db",
                           .name         = visualName,
                           .size         = fileSize,
                           .created      = 0,
                           .lastModified = 0,
                           .type         = Dfs::FileType::Database,
                           .encryption   = Dfs::Encryption::Public,
                           .state        = Dfs::FileState::Loaded };

    auto res = Dfs::Tables::ActorDirFile::addDirRow(actorId, dirRow);

    FragmentStorage fs(actorId, fileId, fileHash);
    fs.initLocalFile(fileSize);
    fs.initHistoricalChain();

    updateDirsLastModified(actorId, dirRow.lastModified);

    insertToFiles(dirRow);
    emit added(dirRow);
    sendFile(actorId, fileId);

    return dirRow;
}

bool DfsController::removeLocalFile(const ActorId &actorId, const std::string &fileId) {
    std::string             path = DfsPath::filePath(actorId, fileId).string();
    DfsP::RemoveFileMessage msg  = { .actorId = actorId, .fileId = fileId };
    bool                    res  = removeFile(msg);
    node->network()->send_message(msg, MessageType::DfsRemoveFile);
    return res;
}

std::string DfsController::addFile(const Dfs::DirRow &dirRow, bool loadBytes) {
    std::string pathDelim       = Utils::platformDelimeter();
    std::string actorFolderPath = DfsB::fsActrRoot + pathDelim + dirRow.actorId.toString() + pathDelim;
    std::string actrDirFilePath = actorFolderPath + DfsB::fsMapName;
    std::string realFilePath    = actorFolderPath + dirRow.fileId;

    if (!writeAvailable(dirRow.size) && !std::filesystem::is_empty(actorFolderPath)) {
        std::vector<std::filesystem::path> files;
        for (const auto &file : std::filesystem::directory_iterator(actorFolderPath)) {
            const auto fileName = file.path().filename();
            if (fileName == DfsB::fsMapName || fileName == DfsB::dsStoreExtention) {
                continue;
            }

            if (file.is_regular_file()) {
                files.push_back(file);
            }
        }

        std::sort(
            files.begin(),
            files.end(),
            [=](const std::filesystem::path p1, const std::filesystem::path p2) {
                return std::filesystem::last_write_time(p1).time_since_epoch()
                       > std::filesystem::last_write_time(p2).time_since_epoch();
            });

        while (!writeAvailable(dirRow.size) || std::filesystem::is_empty(actorFolderPath)) {
            removeLocalFile(dirRow.actorId, files.at(files.size() - 1).string());
        }
    }

    if (loadBytes) {
        if (std::filesystem::exists(realFilePath)) {
            qDebug() << "[Dfs] File already exists"; // temp: not correct, add calc file
            return dirRow.fileId;
        }
        if (!writeAvailable(dirRow.size)) {
            qDebug() << "[Dfs] Storage full";
            qFatal("[Dfs] Storage full");
            return dirRow.fileId;
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

    auto        result       = actrDirFile.select(DfsT::filesTableLast);
    auto        prevRowOpt   = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("fileId") : "";

    DBRow dirRowDb = Utils::toDbRow(dirRow);
    dirRowDb.erase("actorId");
    bool insertRes = actrDirFile.replace(DfsT::ActorDirFile::TableName, dirRowDb);

    if (!insertRes) {
        auto errorStr = fmt::format(
            "[Dfs] addFile: insert failed:{} {}",
            actrDirFile.file().c_str(),
            DfsT::ActorDirFile::TableName.c_str());
        qDebug() << errorStr;
        qFatal("Error 2: %s", errorStr.c_str());
        return "";
    }
    actrDirFile.close();

    DBConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    dirsFile.replace(
        DfsT::DirsFile::TableName,
        { { "actorId", dirRow.actorId.toString() },
          { "lastModified", std::to_string(dirRow.lastModified) } });

    if (loadBytes) {
        if (dirRow.size >= m_bytesLimit - m_sizeTaken) {
            return dirRow.fileId;
        } else {
            DfsP::RequestFileSegmentMessage reqMessage = { .actorId = dirRow.actorId,
                                                           .fileId  = dirRow.fileId,
                                                           .hash    = dirRow.hash,
                                                           .offset  = 0 };
            node->network()->send_message(
                reqMessage,
                MessageType::DfsRequestFileSegment,
                MessageStatus::Request);
        }
    }

    insertToFiles(dirRow);
    emit added(dirRow);

    eLog("[DFS] File {}/{} was added", dirRow.actorId, dirRow.fileId);

    return dirRow.fileId;
}

std::string DfsController::getFileFromStorage(ActorId owner, std::string fileName) {
    auto                  localOwner      = node->accountController()->currentProfile().getActor(owner);
    std::string           pathDelim       = Utils::platformDelimeter();
    const std::string     ownerPath       = DfsB::fsActrRoot + pathDelim + owner.toString() + pathDelim;
    std::filesystem::path realFilePath    = fmt::format("{}{}", ownerPath, fileName);
    std::string           actrDirFilePath = fmt::format("{}{}", ownerPath, DfsB::fsMapName);
    DBConnector           actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        qFatal("Can't open %s", actrDirFilePath.c_str());
        exit(EXIT_FAILURE);
    }

    std::vector<DBRow>    actrDirData  = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, fileName);
    std::filesystem::path tempFilePath = fmt::format("temp{}{}", pathDelim, owner.toString());
    if (!actrDirData.empty()) {
        std::filesystem::path virtualFilePath = actrDirData.at(0).at("filePath");
        if ((virtualFilePath.end()--)->string() == "secured") {
            if (!localOwner->empty()) {
                std::filesystem::create_directories(tempFilePath);
                tempFilePath /= virtualFilePath.filename();
                localOwner->key().decryptFile(realFilePath, tempFilePath);
                return tempFilePath.string();
            }
        }
    }

    return realFilePath.string();
}

bool DfsController::removeFile(const DfsP::RemoveFileMessage &msg) {
    // if (msg.actor != node.accountController()->mainActor()->id().toStdString()) {
    //     qDebug() << "[Dfs] Remove file - file has been removed";
    //     return false;
    // }
    std::string message = fmt::format(
        "[Dfs] Remove file {}. Check equal actors. \"msg.Actor\":{}\n\"mainActor:\"{}",
        msg.fileId,
        msg.actorId,
        node->accountController()->mainActor()->id().toString());
    qDebug() << message;

    auto dirRow = Dfs::Tables::ActorDirFile::getDirRow(msg.actorId, msg.fileId);
    if (!dirRow.has_value()) {
        return false;
    }

    removeRowFromDB(msg);
    std::string path = DfsPath::filePath(msg.actorId, msg.fileId).string();

    {
        QFile file(QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qDebug() << "Could not open VPN localization file:" << file.errorString();
        } else {
            QTextStream          in(&file);
            QString              oneLine = in.readLine();
            static const QString prefix  = "Country:";
            if (oneLine.startsWith(prefix))
                emit getRemovedVPNLocalizationInfo(oneLine, msg.actorId.toString());
        }
    }

    const bool removedFile = std::filesystem::remove(path);
    const bool removeStorjFile =
        std::filesystem::remove(fmt::format("{}{}", path, Dfs::Fragments::Extension));
    message = fmt::format(
        "[Dfs] Remove file {} - {} by path - {}. Storj file has been - {}.",
        msg.fileId,
        (removedFile ? "removed" : "not removed"),
        path,
        removeStorjFile ? "removed" : "not removed");
    qDebug() << message;

    emit removed(dirRow.value());
    return removedFile;
}

std::string DfsController::createFileId(std::filesystem::path file) {
    return createFileIdFromData(file.string());
}

std::string DfsController::createFileIdFromData(const std::string &data) {
    int64_t         time = std::chrono::system_clock::now().time_since_epoch().count();
    boost::mt11213b rng(time);
    boost::random::uniform_int_distribution<> dist(0, INT_MAX);
    std::string                               salt = Tools::typeToStdStringBytes<int>(dist(rng));
    std::string ret = Utils::calcHash(fmt::format("{}{}{}", data, std::to_string(time), salt)).substr(0, 64);
    return ret;
}

bool DfsController::renameFile(
    const ActorId     &actor,
    const std::string &fileHash,
    const std::string &newFileHash) {
    const std::string     actorId   = actor.toString();
    std::string           pathDelim = Utils::platformDelimeter();
    std::filesystem::path path      = DfsB::fsActrRoot + pathDelim + actorId + pathDelim;
    std::filesystem::rename(path / std::string(fileHash), path / std::string(newFileHash));
    return std::filesystem::exists(path / std::string(newFileHash));
}

std::string DfsController::insertFragment(const DfsP::SegmentMessage &msg) {
    qDebug() << "[Dfs] Edit file:" << msg.hash.c_str();
    std::string           pathDelim       = Utils::platformDelimeter();
    std::string           actorPath       = DfsB::fsActrRoot + pathDelim + msg.actorId.toString() + pathDelim;
    std::string           actrDirFilePath = fmt::format("{}{}", actorPath, DfsB::fsMapName);
    std::filesystem::path realFilePath    = fmt::format("{}{}", actorPath, msg.fileId);
    DBConnector           actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, msg.fileId);

    if (actrDirData.empty()) {
        qDebug() << "[Dfs] editFile: Skipped because of empty result";
        qFatal("[Dfs] editFile: Skipped because of empty result: actrDirData || localDirData empty");
        return "";
    }

    if (actrDirData.size() > 2) {
        const auto errorStr = fmt::format(
            "[Dfs] editFile: Query select failed: Query result has unsupported size:{}",
            actrDirData.size());
        qDebug() << QString::fromStdString(errorStr);
        qFatal("Error 4: %s", errorStr.c_str());
        return "";
    }
    insertDataChunk(msg.data, msg.offset, realFilePath);
    actrDirFile.close();
    return Utils::calcHashForFile(realFilePath.string());
}

// void DfsController::addListFiles(const QStringList &files) {
//     qDebug() << "Files add in thread id: [" << QThread::currentThreadId() << "]" << files.size();
//     const auto     actor = node->accountController()->mainActor();
//     ThreadAddFiles addFilesThread(this, actor, files);
//     connect(&addFilesThread, &ThreadAddFiles::added, this,
//             [&](DFSP::AddFileMessage msg, std::string filePath) {
//                 insertToFiles(msg);
//                 emit added(msg.Actor, msg.FileName, msg.Path, msg.Size);
//                 emit resultAddFile("", QString::fromStdString(filePath));
//             });

//     connect(&addFilesThread, &ThreadAddFiles::sendMessage, this,
//             [&](DFSP::AddFileMessage msg, MessageType messageType) {
//                 qDebug() << "send file: " << msg.FileName.c_str();
//                 node->network()->send_message(msg, MessageType::DfsAddFile);
//             });
//     connect(&addFilesThread, &ThreadAddFiles::error, this, [&](std::string error, std::string fileName) {
//         qDebug() << error.c_str();
//         emit resultAddFile(QString::fromStdString(error), QString::fromStdString(fileName));
//     });
//     addFilesThread.start();
//     addFilesThread.wait();
// }

bool DfsController::insertDataChunk(std::string data, std::uint64_t position, std::filesystem::path file) {
    std::string           pathDelim    = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream                     ofs(tempFilePath.string(), std::ios::binary);
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fz = std::filesystem::file_size(file);
    ofs.write(data.c_str(), data.size()); // add data to new temp file
    ofs.flush();
    std::size_t i = 0;
    for (i = position; i < fz; i = i + DfsB::sectionSize) { // copy old data to new temp file
        if (i + DfsB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(
                fmapSource,
                boost::interprocess::read_write,
                i,
                DfsB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(file, position); // cut right side from old file
    std::ofstream                     ofsres(file.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DfsB::sectionSize) { // copy new data to old file
        if (i + DfsB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(
                fmapTarget,
                boost::interprocess::read_write,
                i,
                DfsB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        }
    }
    ofsres.close();

    std::filesystem::remove(tempFilePath);

    return true;
}

bool DfsController::removeDataChunk(
    std::uint64_t         position,
    std::uint64_t         length,
    std::filesystem::path file) {
    std::string           pathDelim    = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = "temp" + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream                     ofs(tempFilePath.string());
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fz = std::filesystem::file_size(file);
    std::size_t                       i  = 0;
    for (i = position + length; i < fz; i = i + DfsB::sectionSize) { // copy old data to new temp file
        if (i + DfsB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(
                fmapSource,
                boost::interprocess::read_write,
                i,
                DfsB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(file, position); // cut right side from old file
    std::ofstream                     ofsres(file.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DfsB::sectionSize) { // copy new data to old file
        if (i + DfsB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(
                fmapTarget,
                boost::interprocess::read_write,
                i,
                DfsB::sectionSize);
            char *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        }
    }
    ofsres.close();

    std::filesystem::remove(tempFilePath);

    return true;
}

std::uint64_t DfsController::sizeTaken() const {
    return m_sizeTaken;
}

std::uint64_t DfsController::totalDfsSize() const {
    return m_totalDfsSize;
}

void DfsController::increaseSizeTaken(uintmax_t value) {
    m_sizeTaken += value;
}

void DfsController::insertToFiles(const Dfs::DirRow &dirRow) {
    files[{ dirRow.actorId, dirRow.fileId }] = dirRow;
}

void DfsController::exportFile(
    const std::string &pathTo,
    const std::string &pathFrom,
    const std::string &nameFile) {
    ActorId actorId;

    if (!std::filesystem::exists(pathTo)) {
        std::filesystem::create_directories(pathTo);
    }

    if (pathFrom.find('/') != std::string::npos) {
        size_t pos = pathFrom.rfind('/');
        actorId    = pathFrom.substr(pos + 1, pathFrom.size());
    } else {
        actorId                               = pathFrom;
        std::filesystem::path actorFolderPath = DfsB::fsActrRoot + "/" + actorId.toString();
        exportFile(pathTo, actorFolderPath.string(), nameFile);
    }

    if (actorId.isZero()) {
        qDebug() << "[Dfs] Path or actor_id hadn't been found. Please check in parameters.";
        return;
    }

    if (!nameFile.empty()) {
        std::string pathFile      = pathFrom + "/" + nameFile;
        const bool  fileFromExist = std::filesystem::exists(pathFile);
        const bool  folderToExist = std::filesystem::exists(pathTo);
        if (fileFromExist && folderToExist) {
            std::filesystem::copy(pathFile, pathTo);
            auto dirRowsExp = Dfs::Tables::ActorDirFile::getDirRows(actorId);
            // TODO: error
            auto dirRows = Dfs::Tables::ActorDirFile::getDirRows(actorId).value();
            auto it      = std::find_if(dirRows.begin(), dirRows.end(), [&](Dfs::DirRow &dirRow) {
                transform(dirRow.fileId.begin(), dirRow.fileId.end(), dirRow.fileId.begin(), ::tolower);
                auto lowerNameFile = nameFile;
                transform(lowerNameFile.begin(), lowerNameFile.end(), lowerNameFile.begin(), ::tolower);
                if (dirRow.fileId == lowerNameFile) {
                    if (!std::filesystem::exists(pathTo + "/" + dirRow.visualPath())) {
                        std::filesystem::rename(pathTo + "/" + nameFile, pathTo + "/" + dirRow.visualPath());
                    } else {
                        const auto pathFile = std::filesystem::path(pathTo + "/" + dirRow.visualPath());
                        for (int index = 2; index < 100; index++) {
                            std::string possibleNewFile = pathTo + "/" + pathFile.stem().string() + "_"
                                                          + std::to_string(index)
                                                          + pathFile.extension().string();
                            if (!std::filesystem::exists(possibleNewFile)) {
                                std::filesystem::rename(pathTo + "/" + nameFile, possibleNewFile);
                                break;
                            }
                        }
                    }
                    qDebug() << fmt::format(
                        "File \"{}\" of actor \"{}\" extracted\n",
                        dirRow.visualPath(),
                        actorId);
                    return true;
                }
                return false;
            });
        }
    } else {
        const std::string nameDirectory = pathTo + "/" + actorId.toString();
        std::filesystem::create_directories(nameDirectory);
        if (pathFrom.find('/') != std::string::npos) {
            for (std::filesystem::directory_entry const &entry :
                 std::filesystem::directory_iterator(pathFrom)) {
                if (entry.path().extension() != DfsF::Extension
                    && entry.path().extension() != DfsF::ExtensionJournal
                    && entry.path().filename() != DfsB::fsMapName) {
                    auto copyTo = (pathTo + "/" + actorId.toString());
                    exportFile(copyTo, pathFrom, entry.path().filename().string());
                }
            }
        }
    }
}

std::uint64_t DfsController::calculateSizeTaken(const std::string &folder) const {
    std::size_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            size += entry.file_size();
        } else if (entry.is_directory()) {
            size += calculateSizeTaken(entry.path().string());
        }
    }

    return size;
}

std::uint64_t DfsController::calculateFilesSize(const std::string &folder) const {
    std::size_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().filename() == Dfs::Basic::fsMapName) {
            const auto actorId = ActorId(entry.path().parent_path().filename().string());
            size += DfsT::ActorDirFile::totalFileSize(actorId);
        } else if (entry.is_directory()) {
            size += calculateFilesSize(entry.path().string());
        }
    }

    return size;
}

std::uint64_t DfsController::calculateDataAmountStored(const std::string &folder) const {
    std::size_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == DfsF::Extension) {
            const auto actorId = ActorId(entry.path().parent_path().filename().string());
            size += DfsT::ActorDirFile::dataAmountStoredSize(actorId, entry.path().filename().string());
        } else if (entry.is_directory()) {
            size += calculateDataAmountStored(entry.path().string());
        }
    }
    return size;
}

std::string DfsController::makeReferenceFile(
    const ActorId                     &actor,
    const std::string                 &nameFile,
    const Dfs::Packets::ReferenceData &referenceData) {
    std::string result;
    result.append(DfsPath::filePath(actor, nameFile).string());
    result.append("|");
    result.append(referenceData.toString());
    return result;
}

void DfsController::dataFromReferenceString(
    const std::string           &referenceStr,
    std::string                 &actor,
    std::string                 &nameFile,
    Dfs::Packets::ReferenceData &referenceData) {
    std::string delimiter    = "|";
    int         posDelimiter = referenceStr.find(delimiter);
    std::string filePath     = referenceStr.substr(0, posDelimiter);
    filePath.erase(0, 4);
    std::string pathdelimiter    = "/";
    int         posPathDelimiter = filePath.find(pathdelimiter);
    actor                        = filePath.substr(0, posPathDelimiter);
    nameFile                     = filePath.substr(posPathDelimiter + 1, filePath.length() - 1);

    std::string referenceDataStr = referenceStr.substr(posDelimiter + 2, referenceStr.size() - 2);
    std::string comadelimiter    = ",";
    std::string keyData          = referenceDataStr.substr(0, referenceDataStr.find(comadelimiter) - 1);
    keyData.erase(0, keyData.find(":") + 2);

    std::string allowData =
        referenceDataStr.substr(referenceDataStr.find(comadelimiter) + 2, referenceDataStr.length() - 2);
    allowData.erase(0, allowData.find(":") + 2);
    allowData.erase(allowData.size() - 2, allowData.size() - 1);
    referenceData = Dfs::Packets::ReferenceData(keyData, allowData);
}

void DfsController::updateDirsLastModified(const ActorId &actorId, uint64_t lastModified) {
    DBConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    dirsFile.replace(
        DfsT::DirsFile::TableName,
        { { "actorId", actorId.toString() }, { "lastModified", std::to_string(lastModified) } });
    dirsFile.close();
}

std::string DfsController::extractFragment(
    boost::interprocess::file_mapping &fmapTarget,
    std::uint64_t                      offset,
    std::uint64_t                      fragmentSize) {
    boost::interprocess::mapped_region rightRegion(
        fmapTarget,
        boost::interprocess::read_only,
        offset,
        fragmentSize);
    char *rr_ptr = static_cast<char *>(rightRegion.get_address());
    return std::string(rr_ptr, fragmentSize);
}

std::string
DfsController::extractFragment(boost::interprocess::file_mapping &fmapTarget, std::uint64_t offset) {
    boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_only, offset);
    char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
    return std::string(rr_ptr, rightRegion.get_size());
}

void DfsController::sendSizeRequestMsg(const ActorId &actorId) const {
    DfsP::RequestDfsSize msg { .actorId = actorId };
    node->network()->send_message(msg, MessageType::RequestDfsSize, MessageStatus::Request);
}

void DfsController::sendSizeReponseMsg(const Dfs::Packets::RequestDfsSize &msg, const std::string &messageId)
    const {
    const auto            dfsSize = calculateSizeTaken();
    DfsP::ResponseDfsSize response { .actorId = msg.actorId, .size = dfsSize };
    node->network()->send_message(response, MessageType::ResponseDfsSize, MessageStatus::Response, messageId);
}

void DfsController::sendCountRequestMsg(const ActorId &actorId) const {
    DfsP::RequestDfsSize msg { .actorId = actorId };
    node->network()->send_message(msg, MessageType::RequestBlockCount, MessageStatus::Request);
}

void DfsController::sendCountReponseMsg(
    const Dfs::Packets::RequestBlockCount &msg,
    const std::string                     &messageId,
    BigNumber                              dfsCount) const {
    DfsP::ResponseBlockCount response { .actorId = msg.actorId, .blockCount = dfsCount };
    node->network()
        ->send_message(response, MessageType::ResponseBlockCount, MessageStatus::Response, messageId);
}

void DfsController::requestSync() {
    node->network()->send_message(
        Utils::currentDateSecs(),
        MessageType::DfsLastModified,
        MessageStatus::Request);
}

void DfsController::requestDirFileAllActors() {
    m_unsynchonizedDirs = node->actorIndex()->allActors();
    if (!m_unsynchonizedDirs.empty())
        requestDirData(ActorId(m_unsynchonizedDirs.at(0)));
}

void DfsController::sendSync(std::uint64_t lastModified, const std::string &messageId) {
    DBConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    auto actors = dirsFile.select(fmt::format(
        "SELECT actorId FROM {} WHERE lastModified = {}",
        DfsT::DirsFile::TableName,
        std::to_string(lastModified)));
    for (auto &row : actors) {
        sendDirData(ActorId(row["actorId"]), lastModified, messageId);
    }
}

void DfsController::requestDirData(const ActorId &actorId) {
    node->network()->send_message(actorId, MessageType::DfsDirData, MessageStatus::Request);
}

void DfsController::sendDirData(
    const ActorId     &actorId,
    std::uint64_t      lastModified,
    const std::string &messageId) {
    if (!std::filesystem::exists(DfsT::ActorDirFile::actorDbPath(actorId))) {
        return;
    }
    auto dirRows = DfsT::ActorDirFile::getDirRows(actorId, lastModified);
    if (!dirRows.has_value())
        return;
    if (!dirRows.value().empty()) {
        node->network()->send_message(
            std::pair { actorId, dirRows.value() },
            MessageType::DfsDirData,
            MessageStatus::Response,
            messageId,
            Config::Net::TypeSend::Focused);
    } else {
        eraseFirstUnsynchronizedDir();
    }
}

void DfsController::addDirData(const ActorId &actorId, const std::vector<Dfs::DirRow> &dirRows) {
    qDebug() << "[Dfs] addDirData result:" << dirRows.size();
    bool res = DfsT::ActorDirFile::addDirRows(actorId, dirRows);
    m_dirRows.insert(std::end(m_dirRows), std::begin(dirRows), std::end(dirRows));

    if (!m_dirRows.empty()) {
        // start fetch fragment
        auto row = m_dirRows[0];
        requestFileSegment(row);
    }
}

void DfsController::requestFile(const ActorId &actorId, const std::string &fileName) {
    qDebug() << fileName.c_str();
    if (fileName.empty())
        return;

    std::filesystem::remove(DfsPath::filePath(actorId, fileName));
    node->network()->send_message(
        std::pair { actorId, fileName },
        MessageType::DfsRequestFile,
        MessageStatus::Request);
}

void DfsController::sendFile(
    const ActorId     &actorId,
    const std::string &fileId,
    const std::string &messageId) {
    if (fileId.empty()) {
        qFatal("[Dfs] Empty file name");
    }

    auto dirRow = DfsT::ActorDirFile::getDirRow(actorId, fileId);

    if (!dirRow.has_value()) {
        return;
    }

    if (messageId.empty()) {
        node->network()->send_message(dirRow.value(), MessageType::DfsAddFile);
    } else {
        node->network()->send_message(
            dirRow.value(),
            MessageType::DfsAddFile,
            MessageStatus::Response,
            messageId,
            Config::Net::TypeSend::Focused);
    }
}

void DfsController::requestFileSegment(const Dfs::DirRow &row) {
    const auto path      = DfsPath::filePath(row.actorId, row.fileId);
    const bool fileExist = std::filesystem::exists(path);
    if (!fileExist) {
        requestFile(row.actorId, row.fileId);
    } else {
        DfsP::RequestFileSegmentMessage reqMessage = { .actorId = row.actorId,
                                                       .fileId  = row.fileId,
                                                       .hash    = row.hash,
                                                       .offset  = 0 };
        node->network()->send_message(reqMessage, MessageType::DfsRequestFileSegment, MessageStatus::Request);
    }
}

void DfsController::beginFetchNextFile() {
    qDebug() << "begin fetch next file";

    if (m_dirRows.empty())
        return;

    m_dirRows.erase(m_dirRows.begin());
    if (!m_dirRows.empty()) {
        auto row = m_dirRows[0];
        requestFileSegment(row);
    }
}

void DfsController::requestNextFragment(const Dfs::Packets::RequestFileSegmentMessage &msg) {
    qDebug() << "request next fragment";
    node->network()->send_message(msg, MessageType::DfsRequestFileSegment, MessageStatus::Request);
}

std::string
DfsController::sendFragment(const DfsP::RequestFileSegmentMessage &msg, const std::string &messageId) {
    std::filesystem::path realFilePath = DfsPath::filePath(msg.actorId, msg.fileId);
    if (!std::filesystem::exists(realFilePath)) {
        return "";
        qFatal("[Dfs] No file");
    }

    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string                       data;
    auto                              fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize - msg.offset > DfsB::sectionSize) {
        data = extractFragment(fmapTarget, msg.offset, DfsB::sectionSize);
    } else {
        data = extractFragment(fmapTarget, msg.offset);
    }

    DfsP::SegmentMessage fragment = { .actorId = msg.actorId,
                                      .fileId  = msg.fileId,
                                      .hash    = msg.hash,
                                      .data    = std::move(data),
                                      .offset  = msg.offset };

    node->network()->send_message(
        fragment,
        MessageType::DfsAddSegment,
        MessageStatus::Response,
        messageId,
        Config::Net::TypeSend::Focused);
    if (msg.offset + DfsB::sectionSize >= fileSize) {
        if (const auto dirRow = Dfs::Tables::ActorDirFile::getDirRow(msg.actorId, msg.fileId);
            dirRow.has_value()) {
            emit uploaded(dirRow.value());
        }
        return "";
    }
    emit uploadProgress(msg.actorId, msg.fileId, double(msg.offset) / double(fileSize) * 100);
    return "";
}

void DfsController::fetchFragments(Dfs::Packets::RequestFileSegmentMessage &msg, std::string &messageId) {
    std::filesystem::path realFilePath = DfsPath::filePath(msg.actorId, msg.fileId);
    if (!std::filesystem::exists(realFilePath)) {
        return;
    }

    auto fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize == 0) {
        return;
    }
    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string                       data;
    std::uint64_t                     totalOffset  = 0;
    bool                              lastFragment = false;
    do {
        std::uint64_t limitSectionSize = 0;
        while (limitSectionSize <= DfsB::maxSectionSize && !lastFragment) {
            if (fileSize - totalOffset > DfsB::sectionSize) {
                data += extractFragment(fmapTarget, totalOffset, DfsB::sectionSize);
                totalOffset += DfsB::sectionSize;
                limitSectionSize += DfsB::sectionSize;
                qDebug() << "progress: [" << (double(totalOffset) / double(fileSize) * 100) << "%]";
                emit uploadProgress(msg.actorId, msg.fileId, double(totalOffset) / double(fileSize) * 100);
            } else {
                lastFragment = true;
                data += extractFragment(fmapTarget, totalOffset);
            }
        }

        DfsP::SegmentMessage fragment = { .actorId = msg.actorId,
                                          .fileId  = msg.fileId,
                                          .hash    = msg.hash,
                                          .data    = std::move(data),
                                          .offset  = totalOffset };

        messageId = node->network()->send_message(
            fragment,
            MessageType::DfsAddSegment,
            MessageStatus::Response,
            messageId,
            Config::Net::TypeSend::Focused);

        if (lastFragment) {
            if (const auto dirRow = Dfs::Tables::ActorDirFile::getDirRow(msg.actorId, msg.fileId);
                dirRow.has_value()) {
                emit uploaded(dirRow.value());
            }
        } else {
            emit uploadProgress(msg.actorId, msg.fileId, double(totalOffset) / double(fileSize) * 100);
        }
    } while (!lastFragment);
}

void DfsController::fetchFragment(Dfs::Packets::RequestFileSegmentMessage &msg, std::string &messageId) {
    std::filesystem::path realFilePath = DfsPath::filePath(msg.actorId, msg.fileId);
    if (!std::filesystem::exists(realFilePath)) {
        return;
    }

    auto fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize == 0) {
        return;
    }
    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string                       data;
    bool                              lastFragment = false;
    std::uint64_t                     totalOffset  = msg.offset;

    std::uint64_t limitSectionSize = 0;
    while (limitSectionSize <= DfsB::maxSectionSize && !lastFragment) {
        if (fileSize - totalOffset > DfsB::sectionSize) {
            data += std::move(extractFragment(fmapTarget, totalOffset, DfsB::sectionSize));
            totalOffset += DfsB::sectionSize;
            limitSectionSize += DfsB::sectionSize;
            qDebug() << "progress: [" << (double(totalOffset) / double(fileSize) * 100) << "%]";
            emit uploadProgress(msg.actorId, msg.fileId, double(totalOffset) / double(fileSize) * 100);
        } else {
            lastFragment = true;
            data += std::move(extractFragment(fmapTarget, totalOffset));
        }
    }

    DfsP::SegmentMessage fragment = { .actorId = msg.actorId,
                                      .fileId  = msg.fileId,
                                      .hash    = msg.hash,
                                      .data    = std::move(data),
                                      .offset  = totalOffset };

    node->network()->send_message(
        fragment,
        MessageType::DfsAddSegment,
        MessageStatus::Response,
        messageId,
        Config::Net::TypeSend::Focused);

    if (lastFragment) {
        if (const auto dirRow = Dfs::Tables::ActorDirFile::getDirRow(msg.actorId, msg.fileId);
            dirRow.has_value()) {
            emit uploaded(dirRow.value());
        }
    } else {
        emit uploadProgress(msg.actorId, msg.fileId, double(totalOffset) / double(fileSize) * 100);
    }
}

void DfsController::verifyFiles(
    std::vector<Dfs::Packets::VerifyFileMessage> &fileList,
    std::string                                  &messageId) {
    for (auto &file : fileList) {
        // check file exist
        std::filesystem::path realFilePath = DfsPath::filePath(file.actorId, file.fileId);
        if (!std::filesystem::exists(realFilePath)) {
            qDebug() << "File by path" << realFilePath.c_str() << "doesn't exist.";
            continue;
        }
        std::string fileHash = Utils::calcHashForFile(realFilePath);
        if (fileHash == file.hash) {
            file.verified = true;
        }
    }
    std::vector<std::string> serializedData = MessagePack::serializeContainer(fileList);
    node->network()->send_message(
        serializedData,
        MessageType::DfsVerifyList,
        MessageStatus::Response,
        messageId,
        Config::Net::TypeSend::Focused);
}

float DfsController::percentVerified(std::vector<Dfs::Packets::VerifyFileMessage> &fileList) {
    float result             = 0.0;
    int   countFilesVerified = 0;
    for (const auto &msg : fileList) {
        if (msg.verified) {
            countFilesVerified++;
        }
    }
    result = ((float)countFilesVerified / (float)fileList.size()) * 100;
    return result;
}

void DfsController::loadBytesLimit() {
    DBConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    const auto rows = dirsFile.select(DfsT::DirsFile::BytesLimitQuery);
    if (!rows.empty()) {
        const auto row = rows.at(0);
        m_bytesLimit   = std::stoll(row.at("value"));
    } else {
        m_bytesLimit = DfsB::minDfsLimit;
    }
    qDebug() << "[Dfs] Limit is" << m_bytesLimit;
    dirsFile.close();
}

void DfsController::eraseFirstUnsynchronizedDir() {
    if (!m_unsynchonizedDirs.empty())
        m_unsynchonizedDirs.erase(m_unsynchonizedDirs.begin());
    if (!m_unsynchonizedDirs.empty())
        requestDirData(ActorId(m_unsynchonizedDirs.at(0)));
}

void DfsController::removeRowFromDB(const Dfs::Packets::RemoveFileMessage &msg) {
    std::string pathDelim = Utils::platformDelimeter();
    std::string actorPath = fmt::format("{}{}{}{}", DfsB::fsActrRoot, pathDelim, msg.actorId, pathDelim);
    std::string actrDirFilePath = fmt::format("{}{}", actorPath, DfsB::fsMapName);
    DBConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }

    std::vector<DBRow> actrDirData = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, msg.fileId);
    std::string        prevHash;
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileId") == msg.fileId) {
            prevHash = it->at("fileIdPrev");
            if (!prevHash.empty()) {
                actrDirFile.update(fmt::format(
                    "UPDATE {} SET fileIdPrev = '{}' WHERE fileIdPrev = '{}'",
                    DfsT::ActorDirFile::TableName,
                    prevHash,
                    it->at("fileId")));
            }
            actrDirFile.query(fmt::format(
                "DELETE FROM {} WHERE fileId='{}'",
                DfsT::ActorDirFile::TableName,
                it->at("fileId")));
        }
    }

    actrDirFile.close();
}

std::string DfsController::addFragment(const DfsP::SegmentMessage &msg) {
    auto fileName = DfsPath::filePath(msg.actorId, msg.fileId);
    if (!std::filesystem::exists(fileName)
        || std::find(m_compliteFiles.begin(), m_compliteFiles.end(), msg.fileId) != m_compliteFiles.end()) {
        return "";
    }

    DBConnector actrDirFile = DfsT::ActorDirFile::actorDbConnector(msg.actorId);
    if (!actrDirFile.isOpen()) {
        qFatal("Error addFragment 1");
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = actrDirFile.select(
        fmt::format("SELECT * FROM {} WHERE fileId = '{}';", DfsT::ActorDirFile::TableName, msg.fileId));
    actrDirFile.close();

    DBRow dirRowDb  = actrDirData[0];
    auto  dirRowExp = Utils::fromDbRow<Dfs::DirRow>(dirRowDb);
    if (!dirRowExp.has_value()) {
        return "expected !has_value()";
    }
    auto dirRow = dirRowExp.value();

    std::string   virtualPath     = dirRow.visualPath();
    std::uint64_t fileSize        = dirRow.size;
    auto          currentFileSize = std::filesystem::file_size(fileName);
    if (fileSize == currentFileSize) {
        m_compliteFiles.push_back(msg.fileId);
        qDebug() << "[Dfs] File is complite";
        return "";
    }

    FragmentStorage fs(msg);
    fs.insertFragment(msg);
    currentFileSize = std::filesystem::file_size(fileName);
    emit downloadProgress(msg.actorId, msg.fileId, double(msg.offset) / double(fileSize) * 100);
    if (fileSize == currentFileSize) {
        if (msg.hash == Utils::calcHashForFile(fileName)) {
            qDebug() << "[Dfs] File" << fileName.c_str() << "done";
            auto dirRow = files.at({ msg.actorId, msg.fileId });
            files.erase({ msg.actorId, msg.fileId });
            emit downloaded(dirRow);
            sendFile(msg.actorId, msg.fileId); // temp
            fs.initHistoricalChain();
            return "hash";
        } else {
            requestFile(msg.actorId, msg.fileId);
            qFatal("[Dfs] Incorrect file check");
            return "";
        }
    }
    return "";
}

void DfsController::threadAddFragment(const Dfs::Packets::SegmentMessage &msg) {
    qDebug() << "add segment. Thread: [" << QThread::currentThreadId() << "]";
    FragmentWriter fw(msg, m_compliteFiles);

    connect(&fw, &FragmentWriter::requestNextFragment, this, &DfsController::requestNextFragment);
    connect(
        &fw,
        &FragmentWriter::downloadProgress,
        this,
        [=, this](const ActorId &actor, const std::string &fileName, const double progress) {
            emit this->downloadProgress(ActorId(actor), fileName, progress);
            this->updateFileState(msg.actorId, msg.fileId, Dfs::FileState::Partially);
        });
    connect(&fw, &FragmentWriter::eraseFromFiles, this, [this](DfsP::SegmentMessage msg) {
        this->files.erase({ msg.actorId, msg.fileId });
    });
    connect(&fw, &FragmentWriter::requestFile, this, &DfsController::requestFile);
    connect(&fw, &FragmentWriter::sendFile, this, &DfsController::sendFile);
    connect(&fw, &FragmentWriter::downloadedFile, this, &DfsController::downloaded);
    connect(&fw, &FragmentWriter::downloadedFile, this, [this](const Dfs::DirRow &dirRow) {
        this->updateFileState(dirRow.actorId, dirRow.fileId, Dfs::FileState::Loaded);
    });

    connect(&fw, &FragmentWriter::compliteFile, this, [this](const std::string &fileName) {
        m_compliteFiles.push_back(fileName);
    });
    connect(&fw, &FragmentWriter::finished, this, &DfsController::beginFetchNextFile);

    fw.start();
    fw.wait();
}

std::string DfsController::deleteFragment(const DfsP::DeleteSegmentMessage &msg) {
    std::string           pathDelim       = Utils::platformDelimeter();
    std::string           pathActor       = DfsB::fsActrRoot + pathDelim + msg.actorId.toString() + pathDelim;
    std::string           actrDirFilePath = fmt::format("{}{}", pathActor, DfsB::fsMapName);
    std::filesystem::path realFilePath    = fmt::format("{}{}", pathActor, msg.hash);
    DBConnector           actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, msg.fileId);

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
    removeDataChunk(msg.offset, msg.size, realFilePath);
    std::string newFileHash = Utils::calcHashForFile(realFilePath.string());
    // std::uint64_t newFileSize = std::filesystem::file_size(realFilePath);

    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("hash") == msg.hash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHash = " +
            // "'"
            //                    + newFileHash + "' " + "WHERE " + "hash = " + "'" +
            //                    it->at("fileHash")
            //                    + "'");
        }
        if (it->at("fileIdPrev") == msg.hash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileIdPrev =
            // " +
            // "'"
            //                    + newFileHash + "' " + "WHERE " + "hash = " + "'" +
            //                    it->at("hash")
            //                    + "'");
        }
    }

    FragmentStorage fragmentStorage(msg.actorId, msg.fileId, msg.hash);
    fragmentStorage.removeFragment(msg);

    return newFileHash;
}

std::uint64_t DfsController::bytesLimit() const {
    return m_bytesLimit;
}

void DfsController::setBytesLimit(std::uint64_t bytesLimit) {
    m_bytesLimit = bytesLimit < DfsB::minDfsLimit ? DfsB::minDfsLimit : bytesLimit;

    DBConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();

    const bool noLimit = dirsFile.select(DfsT::DirsFile::BytesLimitQuery).empty();
    if (!noLimit) {
        dirsFile.update(fmt::format(
            "UPDATE {} SET value='{}' WHERE parameter = '{}'",
            DfsT::DirsFile::ParametersDfs,
            std::to_string(bytesLimit),
            DfsT::DirsFile::BytesLimit));
    } else {
        dirsFile.insert(
            DfsT::DirsFile::ParametersDfs,
            DBRow { { "parameter", DfsT::DirsFile::BytesLimit }, { "value", std::to_string(bytesLimit) } });
    }

    qDebug() << "[Dfs] Changed limit:" << m_bytesLimit;
}

std::uint64_t DfsController::bytesAvailable() {
    auto freeDfs = m_bytesLimit <= m_sizeTaken ? Dfs::Basic::minDfsLimit : m_bytesLimit - m_sizeTaken;
    std::uint64_t freeDisk = Utils::diskFreeMemory();
    auto          min      = m_bytesLimit == 0 ? freeDisk : std::min(freeDfs, freeDisk);
    return min;
}

bool DfsController::writeAvailable(std::size_t size) {
    return bytesAvailable() > size + 10000;
}

void DfsController::updateFileState(
    const ActorId    &actorId,
    const std::string fileName,
    Dfs::FileState    state) {
    auto actrDirFile = DfsT::ActorDirFile::actorDbConnector(actorId);
    actrDirFile.update(fmt::format(
        "UPDATE {} SET state = '{}' WHERE fileId = '{}'",
        DfsT::ActorDirFile::TableName,
        std::to_underlying(state),
        fileName));
    actrDirFile.close();
}

void DfsController::loadVPNLocalizationFiles() {
    DBConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();

    auto actors = dirsFile.select(fmt::format("SELECT actorId FROM {}", DfsT::DirsFile::TableName));
    for (const auto &row : actors) {
        auto        actorId     = ActorId(row.begin()->second);
        DBConnector actrDirFile = DfsT::ActorDirFile::actorDbConnector(actorId);

        auto actorRows = actrDirFile.select(fmt::format(
            "SELECT fileId FROM {} WHERE name='localizationInfo' AND state={}",
            DfsT::ActorDirFile::TableName,
            std::to_string(std::to_underlying(Dfs::FileState::Loaded))));
        for (const auto &actorRow : actorRows) {
            for (const auto &actorCol : actorRow) {
                auto fileName = actorCol.second;
                emit vpnLocalizationLoadedFromStorage(actorId.toString(), fileName);
            }
        }

        actrDirFile.close();
    }

    dirsFile.close();
}
