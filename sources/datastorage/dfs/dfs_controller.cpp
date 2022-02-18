#include "datastorage/dfs/dfs_controller.h"

#include "utils/exc_utils.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>

const QString DFSController::DFSRootDirName = "dfs";
const QString DFSController::DFSDBName = ".dir";
const QString DFSController::DFSService = "Service";

DFSController::DFSController(QObject* parent)
    : QObject(parent),
      m_db(),
      m_db_local()
{
}

DFSController::~DFSController() {
    if (m_db.isOpen()) {
        m_db.close();
    }

    if (m_db_local.isOpen()) {
        m_db_local.close();
    }
}

QByteArray DFSController::addFile(const Actor<KeyPrivate> & actor, const QString & filePath, SecurityLevel securityLevel) {
    if (!QFileInfo::exists(filePath)) {
        qDebug() << "DFSController: addFile: Failed, file does not exists:" << filePath;
        return QByteArray();
    }

    if (!m_db.isOpen())
    {
        qDebug() << "DFSController: addFile: DB open failure:" << m_db.file().c_str();
        return QByteArray();
    }

    if (!m_db_local.isOpen())
    {
        qDebug() << "DFSController: addFile: DB open failure:" << m_db_local.file().c_str();
        return QByteArray();
    }

    const QString actorDirPath = makeActorDirPath(actor);

    createDirectory(actor, securityLevel);
    flushDirContent(actor);

    QByteArray fileHash = Utils::calcKeccakForFile(filePath);
    const QString fileHashPrev = lastHash();

    const QString secureFilePath = makeSecurityDirPath(actor, securityLevel);
    const QString destFilePath = FileSystem::pathConcat(secureFilePath, fileHash);

    switch(securityLevel)
    {
    case SecurityLevel::Private:
    {
        bool fileEnctrypted = Utils::encryptFile(filePath,
                                                 destFilePath,
                                                 QByteArray::fromStdString(actor.key()->secretKey())); // Review here
        if (!fileEnctrypted) {
            qDebug() << "DFSController: addFile: Failed encryptFile:" << filePath;
            return QByteArray();
        }

        // Rename private file because the content has changed after the encription
        fileHash = Utils::calcKeccakForFile(destFilePath);
        const QString encrFilePath = FileSystem::pathConcat(secureFilePath, fileHash);

        if(!QFile::rename(destFilePath, encrFilePath)){
            qDebug() << "DFSController: addFile: Failed rename: " << destFilePath << " to " << encrFilePath;
            return QByteArray();
        }

        break;
    }
    case SecurityLevel::Public:
    {
        bool fileCopied = QFile::copy(filePath, destFilePath);

        if(!fileCopied){
            qDebug() << "DFSController: addFile: Failed to copy file: " << filePath;
            return QByteArray();
        }

        break;
    }
    default:
    {
        qDebug() << "DFSController: addFile: unsupported security level: " << securityLevel;
        return QByteArray();
    }
    }

    // Virtual file path `<root> / <private/public> / <human-readable file name>`
    const QString virtualFilePath = FileSystem::pathConcat(FileSystem::pathConcat(DFSRootDirName,
                                                                                  SecurityLevelName[securityLevel]),
                                                           QFileInfo(filePath).fileName());
    const DBRow rowData = makeDBRow(fileHash, fileHashPrev, virtualFilePath);

    if (!m_db.insert(Config::DataStorage::filesTable, rowData)) {
        qDebug() << "DFSController: addFile: insert failed:" << m_db.file().c_str() << " :"
                 << Config::DataStorage::filesTable.c_str();
        return QByteArray();
    }

    uint64_t lastByteIndex = QFile(filePath).size() - 1;
    const DBRow rowDataWithSegments = makeDBRow(fileHash, fileHashPrev, virtualFilePath, QString::number(0), QString::number(lastByteIndex));

    if (!m_db_local.insert(Config::DataStorage::fileSegmentsTable, rowDataWithSegments)) {
        qDebug() << "DFSController: addFile: insert failed:" << m_db_local.file().c_str() << " :"
                 << Config::DataStorage::fileSegmentsTable.c_str();
        return QByteArray();
    }

    return fileHash;
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

bool DFSController::removeFile(const Actor<KeyPrivate> &actor, const QString & fileHash, SecurityLevel securityLevel) {
    qDebug() << "DFSController: removeFile:" << fileHash;

    const std::string fileHashS = fileHash.toStdString();

    std::map<std::string, DBConnector *> dbList;
    dbList[Config::DataStorage::filesTable] = &m_db;
    dbList[Config::DataStorage::fileSegmentsTable] = &m_db_local;

    for (const auto& [tableName, db]: dbList)
    {
        const std::string selectQuery = (std::stringstream()
                                     << "SELECT * FROM " << tableName
                                     << " WHERE fileHash = '" << fileHashS << "' OR fileHashPrev = '" << fileHashS << "'").str();

        auto result = db->select(selectQuery);

        if (result.empty()) {
            qDebug() << "DFSController: removeFile: Query select skipped because of empty result: Query" << selectQuery.c_str();
            return false;
        }

        if (result.size() > 2) {
            qDebug() << "DFSController: removeFile: Query select failed: Query result has unsupported size:" << result.size()
                     << ": Query:" << selectQuery.c_str();
            return false;
        }

        if (result.size() == 1 && result[0]["fileHashPrev"] == fileHashS) {
            qDebug() << "DFSController: removeFile: Query select failed: fileHashPrev could not be the only field containing the fileHash:"
                     << fileHash << "; Query:" << selectQuery.c_str();
            return false;
        }

        if (result.size() == 2) {
            const std::string & updateRow_fileHash = result[1]["fileHash"];
            const std::string & updateRow_fileHashPrev = result[0]["fileHashPrev"];
            const std::string & updateQuery = (std::stringstream ()
                                               << "UPDATE " << tableName
                                               << " SET fileHashPrev = '" << updateRow_fileHashPrev
                                               << "' WHERE fileHash = '" << updateRow_fileHash << "'").str();
            if (!db->update(updateQuery)) {
                qDebug() << "DFSController: removeFile: Query update failed: New fileHash:" << updateRow_fileHash.c_str()
                         << ", new fileHashPrev:" << updateRow_fileHashPrev.c_str() << ", Query:" << updateQuery.c_str();
                return false;
            }
        }

        if (!db->deleteRow(tableName, result[0])) {
            qDebug() << "DFSController: removeFile: deleteRow filed:" << toString(result[0]);
            return false;
        }
    }

    const QString filePathRemove = FileSystem::pathConcat(makeSecurityDirPath(actor, securityLevel), fileHash);
    if (!QFileInfo::exists(filePathRemove)) {
        qDebug() << "DFSController: removeFile: File does not exists:" << filePathRemove;
        return false;
    }
    if (!QFile().remove(filePathRemove)) {
        qDebug() << "DFSController: removeFile: Remove file failed:" << filePathRemove;
        return false;
    }

    return true;
}

QByteArray DFSController::readFile(const Actor<KeyPrivate> &actor, const QString &fileHash, SecurityLevel securityLevel) {
    qDebug() << "DFSController: readFile:" << fileHash;

    const std::string fileHashS = fileHash.toStdString();
    auto result = findDBRows(fileHashS);

    if (result.empty()) {
        qDebug() << "DFSController: readFile: Skipped because of empty result";
        return QByteArray();
    }

    if (result.size() > 2) {
        qDebug() << "DFSController: readFile: Query select failed: Query result has unsupported size:" << result.size();
        return QByteArray();
    }

    if (result.size() == 1 && result[0]["fileHashPrev"] == fileHashS) {
        qDebug() << "DFSController: readFile: Query select failed: fileHashPrev could not be the only field containing the fileHash:" << fileHash;
        return QByteArray();
    }

    const QString securityDirPathStr = makeSecurityDirPath(actor, securityLevel);
    const QString filePathStr = FileSystem::pathConcat(securityDirPathStr, fileHash);

    if (!QFileInfo::exists(filePathStr)) {
        qDebug() << "DFSController: readFile: File not found:" << filePathStr;
        return QByteArray();
    }

    QByteArray fileContent;

    switch(securityLevel)
    {
    case SecurityLevel::Private:
    {
        fileContent = Utils::decryptFileIntoByteArray(filePathStr, QByteArray::fromStdString(actor.key()->secretKey()));

        break;
    }
    case SecurityLevel::Public:
    {
        auto file = QFile(filePathStr);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "DFSController: readFile: File opening error: " << filePathStr;
            return QByteArray();
        }
        fileContent = file.readAll();
        break;
    }
    default:
    {
        qDebug() << "DFSController: ReadFile: unsupported security level: " << securityLevel;
        return QByteArray();
    }
    }

    return fileContent;
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
// Edit by hash: 22222222: decrypt -> edit -> encrypt: 55555555
//
// [After]
//
//    | fileHash | fileHashPrev | filePath
// ------------------------------------------
//  0 | 11111111 |              | filePath_1
//  1 | 55555555 | 11111111     | filePath_2
//  2 | 33333333 | 55555555     | filePath_3
//

QByteArray DFSController::editFile(const Actor<KeyPrivate> &actor, const QString &fileHash, const QByteArray &fileContent, SecurityLevel securityLevel, uint64_t segmentOffset) {
    qDebug() << "DFSController: editFile:" << fileHash;

    const std::string fileHashS = fileHash.toStdString();
    auto result = findDBRows(fileHashS);

    if (result.empty()) {
        qDebug() << "DFSController: editFile: Skipped because of empty result";
        return QByteArray();
    }

    if (result.size() > 2) {
        qDebug() << "DFSController: editFile: Query select failed: Query result has unsupported size:" << result.size();
        return QByteArray();
    }

    if (result.size() == 1 && result[0]["fileHashPrev"] == fileHashS) {
        qDebug() << "DFSController: editFile: Query select failed: fileHashPrev could not be the only field containing the fileHash:" << fileHash;
        return QByteArray();
    }

    const QString securityDirPathStr = makeSecurityDirPath(actor, securityLevel);
    //const QString filePath = FileSystem::pathConcat(securityDirPathStr, fileHash);

    QByteArray fileContentKeccak = Utils::calcKeccak(fileContent);
    const QString fileNameNewOrigin = fileContentKeccak + "_origin";
    const QString filePathNewOrigin = FileSystem::pathConcat(securityDirPathStr, fileNameNewOrigin);
    const QString filePathNew = FileSystem::pathConcat(securityDirPathStr, fileContentKeccak);

    if (QFileInfo::exists(filePathNewOrigin)) {
        qDebug() << "DFSController: editFile: Failed: New encrypt file name already exists:" << filePathNewOrigin;
        return QByteArray();
    }

    QFile fileNewOrigin(filePathNewOrigin);
    if (!fileNewOrigin.open(QFile::WriteOnly)) {
        qDebug() << "DFSController: editFile: File opening error:" << filePathNewOrigin << fileNewOrigin.errorString();
        return QByteArray();
    }

    if (fileNewOrigin.write(fileContent) == -1) {
        qDebug() << "DFSController: editFile: File write error:" << filePathNewOrigin << fileNewOrigin.errorString();
        return QByteArray();
    }

    fileNewOrigin.close();

    switch(securityLevel)
    {
    case SecurityLevel::Private:
    {
        if (!Utils::encryptFile(filePathNewOrigin, filePathNew, QByteArray::fromStdString(actor.key()->secretKey()))) {
            qDebug() << "DFSController: editFile: Failed encryptFile:" << filePathNewOrigin << "->" << filePathNew;
            QFile().remove(filePathNew);
            QFile().remove(filePathNewOrigin);
            return QByteArray();
        }

        if (!fileNewOrigin.remove(filePathNewOrigin)) {
            qDebug() << "DFSController: editFile: Failed remove temporary data:" << filePathNewOrigin;
            return QByteArray();
        }

        // Rename private file because the content has changed after the encription
        fileContentKeccak = Utils::calcKeccakForFile(filePathNew);
        const QString & encrFilePath = FileSystem::pathConcat(securityDirPathStr, fileContentKeccak);

        if(!QFile::rename(filePathNew, encrFilePath)){
            qDebug() << "DFSController: addFile: Failed rename: " << filePathNew << " to " << encrFilePath;
            return QByteArray();
        }

        break;
    }
    case SecurityLevel::Public:
    {
        if (!fileNewOrigin.rename(filePathNew)) {
            qDebug() << "DFSController: readFile: File opening error: " << filePathNew;
            return QByteArray();
        }

        break;
    }
    default:
    {
        qDebug() << "DFSController: ReadFile: unsupported security level: " << securityLevel;
        return QByteArray();
    }
    }


    //
    // Remove previous content from file system
    //
    const QString filePathPrev = FileSystem::pathConcat(securityDirPathStr, fileHash);
    if (!QFileInfo::exists(filePathPrev)) {
        qDebug() << "DFSController: editFile: Previous file not found:" << filePathPrev;
        return QByteArray(); // Nothing to delete.
    }

    QFile filePrev(filePathPrev);
    if (!filePrev.remove()) {
        qDebug() << "DFSController: editFile: Failed remove old data:" << filePathPrev << filePrev.errorString();
        return QByteArray();
    }

    std::map<std::string, DBConnector *> dbList;
    dbList[Config::DataStorage::filesTable] = &m_db;
    dbList[Config::DataStorage::fileSegmentsTable] = &m_db_local;

    //
    // Update main .dir entry and local copy
    //
    for (const auto& [tableName, db]: dbList)
    {
        if (!setDBFieldValue(*db, tableName.c_str(), "fileHash", fileHash, "fileHash", fileContentKeccak)) {
            return QByteArray();
        }

        // The second row should update fileHashPrev column where previous has to be updated.
        if (result.size() == 2) {
            //if (!_update("fileHashPrev", fileHashS, fileContentKeccak.toStdString())) {
            if (!setDBFieldValue(*db, tableName.c_str(), "fileHashPrev", fileHash, "fileHashPrev", fileContentKeccak)) {
                return QByteArray();
            }
        }
    }

    const uint64_t segmentEndOffset = segmentOffset + fileContent.size() - 1;
    if (!setDBFieldValue(m_db_local, Config::DataStorage::fileSegmentsTable.c_str(),
                         "fileHash", fileContentKeccak, "fileSegmentBegin", QString::number(segmentOffset))) {
        return QByteArray();
    }
    if (!setDBFieldValue(m_db_local, Config::DataStorage::fileSegmentsTable.c_str(),
                         "fileHash", fileContentKeccak, "fileSegmentEnd", QString::number(segmentEndOffset))) {
        return QByteArray();
    }

    return fileContentKeccak;
}

// Verify / Clean zombies / DIR file contains entry, but file system does not contain physical file.
bool DFSController::flushDirContent(const Actor<KeyPrivate> & actor) {
    qDebug() << "DFSController: createDirectory";

    const QString actorDirPath = makeActorDirPath(actor);
    const auto actorFilesTable = m_db.select(Config::DataStorage::filesTableFull);
    const QSet<QString> actorFilesListHashTable = [&] () {
        QSet<QString> result;
        for (auto r : actorFilesTable) {
            result.insert(QString::fromStdString(r["fileHash"]));
        }
        return std::move(result);
    } ();
    const QList<std::tuple<QString, QString>> actorDirList = FileSystem::listFiles(actorDirPath, QStringList() << ".dir");
    for (const std::tuple<QString, QString> & f : actorDirList) {
        if (!actorFilesListHashTable.contains(std::get<0>(f))) {
            const QString & fRemove = std::get<1>(f);
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
bool DFSController::initDB(const Actor<KeyPrivate> & actor)
{
    qDebug() << "DFSController: Instantiate local DB";

    // Step 1: If there is no DB in the User's folder, instantiate new DB

    const QString & actorDirPath = makeActorDirPath(actor);
    const QString & actorDBFilePath = FileSystem::pathConcat(actorDirPath, DFSDBName);

    createDirectory(actor, Private);
    createDirectory(actor, Public);

    initGlobalDB(actor, actorDirPath);

    // Step 2: copy User's DB; if there is a legacy copy DB, replacy it with the global one

    const QString & localDBFileName = actor.id().toString();
    const QString & localDBDirPath = makeServiceDirPath(actor);
    const QString & localDBFilePath = FileSystem::pathConcat(localDBDirPath, localDBFileName);

    if (!QDir().mkpath(localDBDirPath)) {
        qDebug() << "DFSController: createLocalDB: DFS Service dir create error:" << localDBDirPath;
        return false;
    }

    if (QFile::exists(localDBFilePath))
    {
        if(!QFile::remove(localDBFilePath))
        {
            qDebug() << "DFSController: createLocalDB: Remove legacy local DB error:" << localDBFilePath;
            return false;
        }
    }

    if (!QFile::copy(actorDBFilePath, localDBFilePath))
    {
        qDebug() << "DFSController: createLocalDB: Can't copy DB from: " << actorDBFilePath << " to: " << localDBFilePath;
        return false;
    }

    // Step 3: extend the local DB with `fileSegmentBegin` and `fileSegmentEnd` columns

    if(!m_db_local.open(localDBFilePath.toStdString()))
    {
        qDebug() << "DFSController: createLocalDB: Can't open the DB: " << localDBDirPath;
        return false;
    }

    const std::vector<std::string> queryList = {
            // Rename table
        (std::stringstream () << "ALTER TABLE " << Config::DataStorage::filesTable
        << " RENAME TO " << Config::DataStorage::fileSegmentsTable).str(),
            // Add begin segment column
        (std::stringstream () << "ALTER TABLE " << Config::DataStorage::fileSegmentsTable
        << " ADD fileSegmentBegin TEXT NOT NULL DEFAULT(-1)").str(),
            // Add end segment column
        (std::stringstream () << "ALTER TABLE " << Config::DataStorage::fileSegmentsTable
        << " ADD fileSegmentEnd TEXT NOT NULL DEFAULT(-1)").str()};

    for (auto& query: queryList)
    {
        if(!m_db_local.update(query))
        {
            qDebug() << "DFSController: createLocalDB: Can't execute query: " << query.c_str();
            return false;
        }
    }

    return true;
}

QString DFSController::makeActorDirPath(const Actor<KeyPrivate> &actor) {
    return FileSystem::pathConcat(
                FileSystem::pathConcat(QCoreApplication::applicationDirPath(), DFSRootDirName),
                actor.id().toString());
}

QString DFSController::makeSecurityDirPath(const Actor<KeyPrivate> & actor, SecurityLevel securityLevel) {
    return FileSystem::pathConcat(makeActorDirPath(actor), SecurityLevelName[securityLevel]);
}
QString DFSController::makeServiceDirPath(const Actor<KeyPrivate> & actor) {
    return FileSystem::pathConcat(
                FileSystem::pathConcat(QCoreApplication::applicationDirPath(), DFSRootDirName),
                DFSService);
}

QString DFSController::createDirectory(const Actor<KeyPrivate> & actor, SecurityLevel securityLevel) {
    qDebug() << "DFSController: createDirectory";

    QString targetDirPath = FileSystem::pathConcat(makeActorDirPath(actor), SecurityLevelName[securityLevel]);
    if (!QDir().mkpath(targetDirPath)) {
        qDebug() << "DFSController: createDirectory: DFS actor dir create error:" << targetDirPath;
        QCoreApplication::exit(ActorDirCreateError);
    }

    return targetDirPath;
}

// Init the DB in the User's folder
void DFSController::initGlobalDB(const Actor<KeyPrivate> & actor, const QString & sqliteDBTargetPath) {
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
            qDebug() << "DFSController: initDB: Create table failed:" << sqliteDBFilePath << Config::DataStorage::filesTable.c_str();
            QCoreApplication::exit(DBCreateTableError);
        }
    }
}

DBRow DFSController::makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath) {
    return {
        { "fileHash", fileHash.toStdString() },
        { "fileHashPrev", fileHashPrev.toStdString() },
        { "filePath", filePath.toStdString() }
    };
}
DBRow DFSController::makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath,
                const QString & fileSegmentBegin, const QString & fileSegmentEnd){
    return {
        { "fileHash", fileHash.toStdString() },
        { "fileHashPrev", fileHashPrev.toStdString() },
        { "filePath", filePath.toStdString() },
        { "fileSegmentBegin", fileSegmentBegin.toStdString() },
        { "fileSegmentEnd", fileSegmentEnd.toStdString() }
    };
}

DBRow DFSController::findDBRow(DBConnector & db, const QString & tableName, const QString & fileHash)
{
    const std::string fileHashS = fileHash.toStdString();
    const std::string selectQuery = (std::stringstream()
                                     << "SELECT * FROM " << tableName.toStdString()
                                     << " WHERE fileHash = '" << fileHashS << "' OR fileHashPrev = '" << fileHashS << "'").str();
    auto result = db.select(selectQuery);
    return result.size() ? result[0] : DBRow();
}

std::vector<DBRow> DFSController::findDBRows(const std::string &fileHash) {
    const std::string selectQuery = (std::stringstream()
                                     << "SELECT * FROM " << Config::DataStorage::filesTable
                                     << " WHERE fileHash = '" << fileHash << "' OR fileHashPrev = '" << fileHash << "'").str();
    return m_db.select(selectQuery);
}

std::optional<DBRow> DFSController::lastRow() {
    auto result = m_db.select(Config::DataStorage::filesTableLast);
    return result.empty() ? std::optional<DBRow>{} : result[0];
}

QString DFSController::lastHash() {
    auto prevRowOpt = lastRow();
    const QString fileHashPrev = prevRowOpt
            ? QString::fromStdString((*prevRowOpt)["fileHash"])
            : QString::fromStdString("");
    return fileHashPrev;
}

bool DFSController::setDBFieldValue(DBConnector & db,
                                    const QString & tableName,
                                    const QString & searchColumnTitle,
                                    const QString & searchValue,
                                    const QString & changeColumnTitle,
                                    const QString & changeValue)
{
    const std::string & updateQuery = (std::stringstream ()
                                       << "UPDATE " << tableName.toStdString()
                                       << " SET " << changeColumnTitle.toStdString() << " = '" << changeValue.toStdString()
                                       << "' WHERE " << searchColumnTitle.toStdString() << " = '" << searchValue.toStdString() << "'").str();

    qDebug() << updateQuery.c_str();

    if (!db.update(updateQuery)) {
        qDebug() << "DFSController: setDBFieldValue: Query update failed, query:" << updateQuery.c_str();
        return false;
    }

    return true;
}


QByteArray DFSController::addFileSegment(const Actor<KeyPrivate> & actor, const QString & fileHash,
                                   const SecurityLevel securityLevel, const QByteArray &newSegment, uint64_t newSegmentOffset)
{
    if (!m_db.isOpen()) {
        qDebug() << "DFSController: addFileSegment: Failed, DB is not open:" << m_db.file().c_str();
        return QByteArray();
    }

    if (!m_db_local.isOpen()) {
        qDebug() << "DFSController: addFileSegment: Failed, DB is not open:" << m_db_local.file().c_str();
        return QByteArray();
    }

    const QString & securityDirPath = makeSecurityDirPath(actor, securityLevel);
    const QString & filePath = FileSystem::pathConcat(securityDirPath, fileHash);

    // Get dir file info and check
    auto fileInfo = findDBRow(m_db_local, Config::DataStorage::fileSegmentsTable.c_str(), fileHash);

    if(fileInfo.empty())
    {
        // If there is no such file in the DB, you can't download it
        qDebug() << "DFSController: addFileSegment: there is no such file in th DB: " << filePath;
        return QByteArray();
    }

    uint64_t fileBeginOffset = std::stoi(fileInfo["fileSegmentBegin"]);
    uint64_t fileEndOffset = std::stoi(fileInfo["fileSegmentEnd"]);

    if(!QFileInfo::exists(filePath))
    {
        if(fileBeginOffset != -1 || fileEndOffset != -1)
        {
            qDebug() << "DFSController: addFileSegment: local DB and data are not syncronized";
            return QByteArray();
        }

        if(!QFile(filePath).open(QIODevice::WriteOnly))
        {
            qDebug() << "DFSController: addFileSegment: can't create file: " << filePath;
            return QByteArray();
        }

        return editFile(actor, fileHash, newSegment, securityLevel, newSegmentOffset);
    }
    else
    {
        const QString & fileContent = readFile(actor, fileHash, securityLevel);
        if(fileContent.isEmpty())
        {
            qDebug() << "DFSController: addFileSegment: read file error: " << filePath;
            return QByteArray();
        }

        uint64_t newSegmentSize = newSegment.size();

        uint64_t resultBeginOffset = std::min(fileBeginOffset, newSegmentOffset);
        uint64_t resultEndOffset = std::min(fileEndOffset, newSegmentOffset + newSegmentSize);
        uint64_t resultSegmentSize = resultEndOffset - resultBeginOffset;

        std::string resultSegment;
        resultSegment.resize(resultSegmentSize);

        // If segments are overlapped, new segment rewrite old one
        resultSegment.insert(fileBeginOffset - resultBeginOffset, fileContent.toStdString());
        resultSegment.insert(newSegmentOffset - resultBeginOffset, newSegment);

        return editFile(actor, fileHash, resultSegment.c_str(), securityLevel, resultBeginOffset);
    }
}

QByteArray DFSController::deleteFileSegment(const Actor<KeyPrivate> &actor, const QString &fileHash,
                                      const SecurityLevel securityLevel, uint64_t segmentOffset, uint64_t segmentSize)
{
    if (!m_db.isOpen()) {
        qDebug() << "DFSController: deleteFileSegment: Failed, DB is not open:" << m_db.file().c_str();
        return QByteArray();
    }

    if (!m_db_local.isOpen()) {
        qDebug() << "DFSController: deleteFileSegment: Failed, DB is not open:" << m_db_local.file().c_str();
        return QByteArray();
    }

    const QString & securityDirPath = makeSecurityDirPath(actor, securityLevel);
    const QString & filePath = FileSystem::pathConcat(securityDirPath, fileHash);

    // Get dir file info and check
    auto fileInfo = findDBRow(m_db_local, Config::DataStorage::fileSegmentsTable.c_str(), fileHash);

    if(fileInfo.empty())
    {
        // If there is no such file in the DB, you can't download it
        qDebug() << "DFSController: deleteFileSegment: there is no such file in th DB: " << filePath;
        return QByteArray();
    }

    uint64_t fileBeginOffset = std::stoi(fileInfo["fileSegmentBegin"]);
    uint64_t fileEndOffset = std::stoi(fileInfo["fileSegmentEnd"]);

    if(!QFileInfo::exists(filePath))
    {
        qDebug() << "DFSController: deleteFileSegment: there is no file on the drive";
        return QByteArray();
    }
    else
    {
        if(fileBeginOffset != segmentOffset && fileEndOffset != segmentOffset + segmentSize)
        {
            qDebug() << "DFSController: deleteFileSegment: segments are not attached to the begin or end of the file chunk";
            return QByteArray();
        }

        const QString & fileContent = readFile(actor, fileHash, securityLevel);
        if(fileContent.isEmpty())
        {
            qDebug() << "DFSController: deleteFileSegment: read file error: " << filePath;
            return QByteArray();
        }

        auto prevSegment = fileContent.toStdString();
        auto & resultSegment = prevSegment.erase(segmentOffset, segmentSize);

        uint64_t newFileOffset = segmentOffset == fileBeginOffset ? segmentOffset + segmentSize : fileBeginOffset;
        return editFile(actor, fileHash, resultSegment.c_str(), securityLevel, newFileOffset);
    }
}

