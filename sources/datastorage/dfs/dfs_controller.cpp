#include "datastorage/dfs/dfs_controller.h"

const QString DFSController::DFSRootDirName = "dfs";
const QString DFSController::DFSDBName = ".dir";
const QString DFSController::DFSService = "Service";

DFSController::DFSController(std::shared_ptr<ActorIndex> ActorIndex,
                             std::shared_ptr<AccountController> AccountController, QObject *parent)
    : QObject(parent) {
    actorIndex = ActorIndex;
    accountController = AccountController;
}

DFSController::~DFSController() {
}

std::string DFSController::addLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath,
                                        std::string targetVirtualFilePath, DFS::Encryption securityLevel) {
    std::string pathDelim = Utils::getPlatformDelimeter();
    std::string newFilePath = filePath;
    std::string newTargetVirtualFilePath = targetVirtualFilePath;
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
    msg.actorId = actor.id().toStdString();
    msg.fileHash = fileHash;
    msg.path = newTargetVirtualFilePath;
    msg.size = std::filesystem::file_size(newFilePath);

    addFile(msg, false);
}

std::string DFSController::addFile(const DFS::Packets::AddFileMessage &msg, bool loadBytes) {
    std::string pathDelim = Utils::getPlatformDelimeter();
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + msg.actorId + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::create_directories(DFS::Basic::serviceDfsPath + pathDelim + msg.actorId);

    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.actorId + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::create_directories(DFS::Basic::fsActrRoot + pathDelim + msg.actorId);

    std::string realFilePath = DFS::Basic::fsActrRoot + pathDelim + msg.actorId + pathDelim + msg.fileHash;
    std::filesystem::create_directories(DFS::Basic::fsActrRoot + pathDelim + msg.actorId);

    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }

    auto existingRowsActrDirFile = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, msg.fileHash);
    auto existingRowsLocalDirFile = DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, msg.fileHash);
    if (existingRowsActrDirFile.size() != 0 && existingRowsLocalDirFile.size() != 0) {
        actrDirFile.close();
        localDirFile.close();
        return msg.fileHash;
    }
    actrDirFile.query(DFS::Tables::ActorDirFile::CreateTableQuery);

    auto result = actrDirFile.select(DFS::Tables::filesTableLast);
    auto prevRowOpt = result.empty() ? std::optional<DBRow> {} : result[0];
    std::string lastFileHash = prevRowOpt ? prevRowOpt->at("fileHash") : "";

    const DBRow rowData = makeActrDirDBRow(msg.fileHash, lastFileHash, msg.path, msg.size);

    if (!actrDirFile.insert(DFS::Tables::ActorDirFile::TableName, rowData)) {
        qDebug() << "DFSController: addFile: insert failed:" << actrDirFile.file().c_str() << " :"
                 << DFS::Tables::ActorDirFile::TableName.c_str();
        return "";
    }
    actrDirFile.close();

    localDirFile.query(DFS::Tables::LocalDirFile::CreateTableQuery);

    DBRow rowDataWithSegments;
    if (loadBytes) {
        rowDataWithSegments = makeLocalDirDBRow(msg.fileHash, lastFileHash, msg.path, -1, -1, msg.size);
    } else {
        rowDataWithSegments = makeLocalDirDBRow(msg.fileHash, lastFileHash, msg.path, 0, msg.size, msg.size);
    }
    if (!localDirFile.insert(DFS::Tables::LocalDirFile::TableName, rowDataWithSegments)) {
        qDebug() << "DFSController: addFile: insert failed:" << localDirFile.file().c_str() << " :"
                 << DFS::Tables::LocalDirFile::TableName.c_str();
        return "";
    }
    localDirFile.close();
    if (loadBytes) {
        // TODO: init segment loading
    }
    return msg.fileHash;
}

std::string DFSController::getFileFromStorage(ActorId owner, std::string fileHash) {
    Actor<KeyPrivate> localOwner = accountController->getActor(owner);
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
    qDebug() << "DFSController: removeFile:" << msg.fileHash.c_str();
    std::string pathDelim = Utils::getPlatformDelimeter();
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + msg.actorId + pathDelim + DFS::Basic::fsMapName;
    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.actorId + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::path realFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.actorId + pathDelim + msg.fileHash;
    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, msg.fileHash);
    std::vector<DBRow> localDirData =
        DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, msg.fileHash);
    std::string prevHash;
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.fileHash) {
            prevHash = it->at("fileHashPrev");
            actrDirFile.deleteRow(DFS::Tables::ActorDirFile::TableName, *it);
            if (!std::filesystem::remove(realFilePath)) {
                qDebug() << "File removal by path " << realFilePath.c_str() << " failed";
                return false;
            }
        }
        if ((it->at("fileHashPrev") == msg.fileHash) && (!prevHash.empty())) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHashPrev = " + "'"
                               + prevHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }
    for (auto it = localDirData.begin(); it < localDirData.end(); it++) {
        if (it->at("fileHash") == msg.fileHash) {
            prevHash = it->at("fileHashPrev");
            localDirFile.deleteRow(DFS::Tables::LocalDirFile::TableName, *it);
            if (!std::filesystem::remove(realFilePath)) {
                qDebug() << "File removal by path " << realFilePath.c_str() << " failed";
                return false;
            }
        }
        if ((it->at("fileHashPrev") == msg.fileHash) && (!prevHash.empty())) {
            localDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName
                                + " SET fileHashPrev = " + "'" + prevHash + "' " + "WHERE "
                                + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }
    actrDirFile.close();
    localDirFile.close();

    return true;
}

std::string DFSController::insertFragment(const Actor<KeyPrivate> &actor,
                                          const DFS::Packets::EditSegmentMessage &msg) {
    qDebug() << "DFSController: editFile:" << msg.fileHash.c_str();
    std::string pathDelim = Utils::getPlatformDelimeter();
    DBConnector localDirFile;
    std::string localDirFilePath =
        DFS::Basic::serviceDfsPath + pathDelim + msg.actorId + pathDelim + DFS::Basic::fsMapName;
    DBConnector actrDirFile;
    std::string actrDirFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.actorId + pathDelim + DFS::Basic::fsMapName;
    std::filesystem::path realFilePath =
        DFS::Basic::fsActrRoot + pathDelim + msg.actorId + pathDelim + msg.fileHash;
    if (!actrDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    if (!localDirFile.open(actrDirFilePath)) {
        exit(EXIT_FAILURE);
    }
    std::vector<DBRow> actrDirData = DFS::Tables::ActorDirFile::getFileDataByHash(&actrDirFile, msg.fileHash);
    std::vector<DBRow> localDirData =
        DFS::Tables::LocalDirFile::getFileDataByHash(&localDirFile, msg.fileHash);

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
        if (it->at("fileHash") == msg.fileHash) {
            if ((std::stoul(it->at("fileSegmentBegin")) > msg.offset)
                || (std::stoul(it->at("fileSegmentEnd")) < (msg.offset + msg.data.size()))) {
                return msg.fileHash;
            }
        }
    }

    insertDataChunk(msg.data, msg.offset, realFilePath);
    std::string newFileHash = Utils::calcKeccakForFile(realFilePath.string());
    unsigned int newFileSize = std::filesystem::file_size(realFilePath);
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("fileHash") == msg.fileHash) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHash = " + "'"
                               + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                               + "'");
        }
        if (it->at("fileHashPrev") == msg.fileHash) {
            actrDirFile.update("UPDATE " + DFS::Tables::ActorDirFile::TableName + " SET fileHashPrev = " + "'"
                               + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                               + "'");
        }
    }
    for (auto it = localDirData.begin(); it < localDirData.end(); it++) {
        if (it->at("fileHash") == msg.fileHash) {
            localDirFile.update("UPDATE " + DFS::Tables::LocalDirFile::TableName + " SET fileHash = " + "'"
                                + newFileHash + "' " + "WHERE " + "fileHash = " + "'" + it->at("fileHash")
                                + "'");
        }
        if (it->at("fileHashPrev") == msg.fileHash) {
            localDirFile.update("UPDATE " + DFS::Tables::LocalDirFile::TableName
                                + " SET fileHashPrev = " + "'" + newFileHash + "' " + "WHERE "
                                + "fileHash = " + "'" + it->at("fileHash") + "'");
        }
    }

    // TODO: InsertMessage processing (edit rows in correspondind dbs)
    return newFileHash;
}

// Verify / Clean zombies / DIR file contains entry, but file system does not contain physical file.
bool DFSController::flushDirContent(const QString &userId) {
    qDebug() << "DFSController: createDirectory";

    const QString actorDirPath = makeActorDirPath(userId);
    const auto actorFilesTable = m_db.select(Config::DataStorage::filesTableFull);
    const QSet<QString> actorFilesListHashTable = [&]() {
        QSet<QString> result;
        for (auto r : actorFilesTable) {
            result.insert(QString::fromStdString(r["fileHash"]));
        }
        return std::move(result);
    }();
    const QList<std::tuple<QString, QString>> actorDirList =
        FileSystem::listFiles(actorDirPath, QStringList() << ".dir");
    for (const std::tuple<QString, QString> &f : actorDirList) {
        if (!actorFilesListHashTable.contains(std::get<0>(f))) {
            const QString &fRemove = std::get<1>(f);
            if (!QFile().remove(fRemove)) {
                qDebug() << "DFSController: validateDirectory: Remove file failed:" << fRemove;
                // To discuss: Shoud the function return false is a single file removal has failed?
                // return false;
            }
        }
    }
    return true;
}

// Copy and extend the User DB / Neccessary initialization step
bool DFSController::initDB(const Actor<KeyPrivate> &actor) {
    qDebug() << "DFSController: Instantiate local DB";

    // Step 1: If there is no DB in the User's folder, instantiate new DB

    const QString &actorDirPath = makeActorDirPath(actor.idStd().c_str());
    const QString &actorDBFilePath = FileSystem::pathConcat(actorDirPath, DFSDBName);

    createDirectory(actorDirPath);

    initGlobalDB(actorDirPath);

    // Step 2: copy User's DB; if there is a legacy copy DB, replacy it with the global one

    const QString &localDBFileName = actor.id().toString();
    const QString &localDBDirPath = makeServiceDirPath(actor);
    const QString &localDBFilePath = FileSystem::pathConcat(localDBDirPath, localDBFileName);

    if (!QDir().mkpath(localDBDirPath)) {
        qDebug() << "DFSController: createLocalDB: DFS Service dir create error:" << localDBDirPath;
        return false;
    }

    if (QFile::exists(localDBFilePath)) {
        if (!QFile::remove(localDBFilePath)) {
            qDebug() << "DFSController: createLocalDB: Remove legacy local DB error:" << localDBFilePath;
            return false;
        }
    }

    if (!QFile::copy(actorDBFilePath, localDBFilePath)) {
        qDebug() << "DFSController: createLocalDB: Can't copy DB from: " << actorDBFilePath
                 << " to: " << localDBFilePath;
        return false;
    }

    // Step 3: extend the local DB with `fileSegmentBegin` and `fileSegmentEnd` columns

    if (!m_db_local.open(localDBFilePath.toStdString())) {
        qDebug() << "DFSController: createLocalDB: Can't open the DB: " << localDBDirPath;
        return false;
    }

    const std::vector<std::string> queryList = {
        // Rename table
        (std::stringstream() << "ALTER TABLE " << Config::DataStorage::filesTable << " RENAME TO "
                             << Config::DataStorage::fileSegmentsTable)
            .str(),
        // Add begin segment column
        (std::stringstream() << "ALTER TABLE " << Config::DataStorage::fileSegmentsTable
                             << " ADD fileSegmentBegin TEXT NOT NULL DEFAULT(-1)")
            .str(),
        // Add end segment column
        (std::stringstream() << "ALTER TABLE " << Config::DataStorage::fileSegmentsTable
                             << " ADD fileSegmentEnd TEXT NOT NULL DEFAULT(-1)")
            .str()
    };

    for (auto &query : queryList) {
        if (!m_db_local.update(query)) {
            qDebug() << "DFSController: createLocalDB: Can't execute query: " << query.c_str();
            return false;
        }
    }

    return true;
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
    std::size_t regSize = 256;
    std::size_t i = 0;
    for (i = position; i < fz; i = i + regSize) { // copy old data to new temp file
        if (i + regSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i,
                                                           regSize);
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

    for (i = 0; i < fzres; i = i + regSize) { // copy new data to old file
        if (i + regSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i,
                                                           regSize);
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

QString DFSController::makeActorDirPath(const QString &actorId) {
    return FileSystem::pathConcat(
        FileSystem::pathConcat(QCoreApplication::applicationDirPath(), DFSRootDirName), actorId);
}

QString DFSController::makeSecurityDirPath(const Actor<KeyPrivate> &actor, SecurityLevel securityLevel) {
    return FileSystem::pathConcat(makeActorDirPath(actor.idStd().c_str()), SecurityLevelName[securityLevel]);
}
QString DFSController::makeServiceDirPath(const Actor<KeyPrivate> &actor) {
    return FileSystem::pathConcat(
        FileSystem::pathConcat(QCoreApplication::applicationDirPath(), DFSRootDirName), DFSService);
}

QString DFSController::makeGlobalPath(const QString &virtualPath, const QString &userId) {
    qDebug() << virtualPath.split('/')[0] << " " << virtualPath.split('/')[1];
    QString dirName = virtualPath.split('/')[0];
    QString securityLevel = virtualPath.split('/')[1];

    qDebug() << FileSystem::pathConcat(QCoreApplication::applicationDirPath(), dirName);
    qDebug() << FileSystem::pathConcat(userId, securityLevel);

    return FileSystem::pathConcat(FileSystem::pathConcat(QCoreApplication::applicationDirPath(), dirName),
                                  FileSystem::pathConcat(userId, securityLevel));
}

DFSController::SecurityLevel DFSController::getSecurityLevel(const QString &virtualPath) {
    for (const auto &word : virtualPath.split('/')) {
        for (const auto &securityWord : SecurityLevelName) {
            if (word == securityWord) {
                const auto &wordIndex = SecurityLevelName.indexOf(securityWord);
                return static_cast<SecurityLevel>(wordIndex);
            }
        }
    }

    qDebug() << __FUNCTION__ << " Invalid path, has to contain the securityLevel";
    return SecurityLevel::Public;
}

bool DFSController::createDirectory(const QString &path) {
    qDebug() << "DFSController: createDirectory: " << path.toShort();

    if (!QDir().mkpath(path)) {
        qDebug() << "DFSController: createDirectory: DFS actor dir create error:" << path;
        QCoreApplication::exit(ActorDirCreateError);
    }

    return true;
}

// Init the DB in the User's folder
void DFSController::initGlobalDB(const QString &sqliteDBTargetPath) {
    qDebug() << "DFSController: initDB:" << sqliteDBTargetPath;

    QString sqliteDBFilePath = FileSystem::pathConcat(sqliteDBTargetPath, DFSDBName);
    const bool dbExists = QFileInfo::exists(sqliteDBFilePath);

    if (!m_db.open(sqliteDBFilePath.toStdString())) {
        qDebug() << "DFSController: initDB: Can't open DB:" << sqliteDBFilePath;
        QCoreApplication::exit(DBOpenError);
    }

    if (!dbExists) {
        qDebug() << "DFSController: initDB: Create table:" << Config::DataStorage::filesTable.c_str();
        if (!m_db.createTable(Config::DataStorage::filesTableCreate)) {
            qDebug() << "DFSController: initDB: Create table failed:" << sqliteDBFilePath
                     << Config::DataStorage::filesTable.c_str();
            QCoreApplication::exit(DBCreateTableError);
        }
    }
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

DBRow DFSController::findDBRow(DBConnector &db, const QString &tableName, const QString &fileHash) {
    const std::string fileHashS = fileHash.toStdString();
    const std::string selectQuery =
        (std::stringstream() << "SELECT * FROM " << tableName.toStdString() << " WHERE fileHash = '"
                             << fileHashS << "' OR fileHashPrev = '" << fileHashS << "'")
            .str();
    auto result = db.select(selectQuery);
    return result.size() ? result[0] : DBRow();
}

std::vector<DBRow> DFSController::findDBRows(const std::string &fileHash) {
    const std::string selectQuery =
        (std::stringstream() << "SELECT * FROM " << Config::DataStorage::filesTable << " WHERE fileHash = '"
                             << fileHash << "' OR fileHashPrev = '" << fileHash << "'")
            .str();
    return m_db.select(selectQuery);
}

bool DFSController::setDBFieldValue(DBConnector &db, const QString &tableName,
                                    const QString &searchColumnTitle, const QString &searchValue,
                                    const QString &changeColumnTitle, const QString &changeValue) {
    const std::string &updateQuery =
        (std::stringstream() << "UPDATE " << tableName.toStdString() << " SET "
                             << changeColumnTitle.toStdString() << " = '" << changeValue.toStdString()
                             << "' WHERE " << searchColumnTitle.toStdString() << " = '"
                             << searchValue.toStdString() << "'")
            .str();

    qDebug() << updateQuery.c_str();

    if (!db.update(updateQuery)) {
        qDebug() << "DFSController: setDBFieldValue: Query update failed, query:" << updateQuery.c_str();
        return false;
    }

    return true;
}

QByteArray DFSController::addFileSegment(const Actor<KeyPrivate> &actor, const AddSegmentMsg &msg) {
    const QString &userId = msg.userId.c_str();
    const QString &fileHash = msg.fileHash.c_str();
    const QByteArray &newSegment = msg.data.c_str();
    uint64_t newSegmentOffset = std::stoll(msg.offset);

    if (!m_db.isOpen()) {
        qDebug() << "DFSController: addFileSegment: Failed, DB is not open:" << m_db.file().c_str();
        return QByteArray();
    }

    if (!m_db_local.isOpen()) {
        qDebug() << "DFSController: addFileSegment: Failed, DB is not open:" << m_db_local.file().c_str();
        return QByteArray();
    }

    // Get dir file info and check
    auto fileInfo = findDBRow(m_db_local, Config::DataStorage::fileSegmentsTable.c_str(), fileHash);

    if (fileInfo.empty()) {
        // If there is no such file in the DB, you can't download it
        qDebug() << "DFSController: addFileSegment: there is no such file in th DB: " << fileHash;
        return QByteArray();
    }

    const QString &securityDirPath = makeGlobalPath(fileInfo["filePath"].c_str(), userId);
    const QString &filePath = FileSystem::pathConcat(securityDirPath, fileHash);

    uint64_t fileBeginOffset = std::stoi(fileInfo["fileSegmentBegin"]);
    uint64_t fileEndOffset = std::stoi(fileInfo["fileSegmentEnd"]);

    if (!QFileInfo::exists(filePath)) {
        if (fileBeginOffset != -1 || fileEndOffset != -1) {
            qDebug() << "DFSController: addFileSegment: local DB and data are not syncronized";
            return QByteArray();
        }

        return insertFragment(actor,
                              { userId.toStdString(), fileHash.toStdString(), newSegment.toStdString(),
                                std::to_string(newSegmentOffset) });
    } else {
        const QString &fileContent = readFile(actor, fileHash);
        if (fileContent.isEmpty()) {
            qDebug() << "DFSController: addFileSegment: read file error: " << filePath;
            return QByteArray();
        }

        long long newSegmentSize = newSegment.size();

        long long resultBeginOffset = std::min(fileBeginOffset, newSegmentOffset);
        long long resultEndOffset = std::min(fileEndOffset, newSegmentOffset + newSegmentSize);
        long long resultSegmentSize = resultEndOffset - resultBeginOffset;

        std::string resultSegment;
        resultSegment.resize(resultSegmentSize);

        // If segments are overlapped, new segment rewrite old one
        resultSegment.insert(fileBeginOffset - resultBeginOffset, fileContent.toStdString());
        resultSegment.insert(newSegmentOffset - resultBeginOffset, newSegment);

        return insertFragment(
            actor, { userId.toStdString(), fileHash.toStdString(), resultSegment, resultBeginOffset });
    }
}

QByteArray DFSController::deleteFileSegment(const Actor<KeyPrivate> &actor, const DeleteSegmentMsg &msg) {
    const QString &userId = msg.userId.c_str();
    const QString &fileHash = msg.fileHash.c_str();
    uint64_t segmentOffset = std::stoll(msg.offset);
    uint64_t segmentSize = std::stoll(msg.size);

    if (!m_db.isOpen()) {
        qDebug() << "DFSController: deleteFileSegment: Failed, DB is not open:" << m_db.file().c_str();
        return QByteArray();
    }

    if (!m_db_local.isOpen()) {
        qDebug() << "DFSController: deleteFileSegment: Failed, DB is not open:" << m_db_local.file().c_str();
        return QByteArray();
    }

    // Get dir file info and check
    auto fileInfo = findDBRow(m_db_local, Config::DataStorage::fileSegmentsTable.c_str(), fileHash);

    if (fileInfo.empty()) {
        // If there is no such file in the DB, you can't download it
        qDebug() << "DFSController: deleteFileSegment: there is no such file in th DB: " << fileHash;
        return QByteArray();
    }

    const QString &securityDirPath = makeGlobalPath(fileInfo["filePath"].c_str(), userId);
    const QString &filePath = FileSystem::pathConcat(securityDirPath, fileHash);

    uint64_t fileBeginOffset = std::stoi(fileInfo["fileSegmentBegin"]);
    uint64_t fileEndOffset = std::stoi(fileInfo["fileSegmentEnd"]);

    if (!QFileInfo::exists(filePath)) {
        qDebug() << "DFSController: deleteFileSegment: there is no file on the drive";
        return QByteArray();
    } else {
        if (fileBeginOffset != segmentOffset && fileEndOffset != segmentOffset + segmentSize) {
            qDebug() << "DFSController: deleteFileSegment: segments are not attached to the begin or end of "
                        "the file chunk";
            return QByteArray();
        }

        const QString &fileContent = readFile(actor, fileHash);
        if (fileContent.isEmpty()) {
            qDebug() << "DFSController: deleteFileSegment: read file error: " << filePath;
            return QByteArray();
        }

        auto prevSegment = fileContent.toStdString();
        auto &resultSegment = prevSegment.erase(segmentOffset, segmentSize);

        uint64_t newFileOffset =
            segmentOffset == fileBeginOffset ? segmentOffset + segmentSize : fileBeginOffset;
        return insertFragment(
            actor,
            { userId.toStdString(), fileHash.toStdString(), resultSegment, std::to_string(newFileOffset) });
    }
}
