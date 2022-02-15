#include "datastorage/dfs/dfs_controller.h"

#include "utils/exc_utils.h"

#include <QDir>
#include <QFileInfo>
#include <QDebug>

const QString DFSController::DFSRootDirName = "dfs";
const QString DFSController::DFSDBName = ".dir";

DFSController::DFSController(QObject* parent)
    : QObject(parent)
    , m_db()
{
}

DFSController::~DFSController() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

QByteArray DFSController::addFile(const Actor<KeyPrivate> & actor, const QString & filePath) {
    if (!QFileInfo::exists(filePath)) {
        qDebug() << "DFSController: addFile: Failed, file does not exists:" << filePath;
        return QByteArray();
    }

    const QString actorDirPath = createDirectory(actor);
    initDB(actor, actorDirPath);
    flushDirContent(actor);

    if (!m_db.isOpen()) {
        qDebug() << "DFSController: addFile: Failed, DB is not open:" << m_db.file().c_str();
        return QByteArray();
    }

    const QByteArray fileHash = Utils::calcKeccakForFile(filePath);
    const QString fileHashPrev = lastHash();
    const QString fileName = FileSystem::pathConcat(DFSRootDirName, QFileInfo(filePath).fileName());
    const DBRow rowData = makeDBRow(fileHash, fileHashPrev, fileName);

    const QString destFilePath = FileSystem::pathConcat(actorDirPath, fileHash);
    bool fileEnctrypted = Utils::encryptFile(filePath,
                                             destFilePath,
                                             QByteArray::fromStdString(actor.key()->secretKey())); // Review here
    if (!fileEnctrypted) {
        qDebug() << "DFSController: addFile: Failed encryptFile:" << filePath;
        return QByteArray();
    }

    if (!m_db.insert(Config::DataStorage::filesTable, rowData)) {
        qDebug() << "DFSController: addFile: insert failed:" << m_db.file().c_str() << " :" << Config::DataStorage::filesTable.c_str();
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

bool DFSController::removeFile(const Actor<KeyPrivate> &actor, const QString & fileHash) {
    qDebug() << "DFSController: removeFile:" << fileHash;

    const std::string fileHashS = fileHash.toStdString();
    const std::string selectQuery = (std::stringstream()
                                     << "SELECT * FROM " << Config::DataStorage::filesTable
                                     << " WHERE fileHash = '" << fileHashS << "' OR fileHashPrev = '" << fileHashS << "'").str();
    auto result = m_db.select(selectQuery);

    if (result.empty()) {
        qDebug() << "DFSController: removeFile: Query select skipped because of empty result: Query" << selectQuery.c_str();
        return false;
    }

    if (result.size() > 2) {
        qDebug() << "DFSController: removeFile: Query select failed: Query result has unsupported size:" << result.size() << ": Query:" << selectQuery.c_str();
        return false;
    }

    if (result.size() == 1 && result[0]["fileHashPrev"] == fileHashS) {
        qDebug() << "DFSController: removeFile: Query select failed: fileHashPrev could not be the only field containing the fileHash:" << fileHash << "; Query:" << selectQuery.c_str();
        return false;
    }

    if (result.size() == 2) {
        const std::string & updateRow_fileHash = result[1]["fileHash"];
        const std::string & updateRow_fileHashPrev = result[0]["fileHashPrev"];
        const std::string & updateQuery = (std::stringstream ()
                                           << "UPDATE " << Config::DataStorage::filesTable
                                           << " SET fileHashPrev = '" << updateRow_fileHashPrev
                                           << "' WHERE fileHash = '" << updateRow_fileHash << "'").str();
        if (!m_db.update(updateQuery)) {
            qDebug() << "DFSController: removeFile: Query update failed: New fileHash:" << updateRow_fileHash.c_str() << ", new fileHashPrev:" << updateRow_fileHashPrev.c_str() << ", Query:" << updateQuery.c_str();
            return false;
        }
    }

    if (!m_db.deleteRow(Config::DataStorage::filesTable, result[0])) {
        qDebug() << "DFSController: removeFile: deleteRow filed:" << toString(result[0]);
        return false;
    }

    const QString filePathRemove = FileSystem::pathConcat(makeActorDirPath(actor), fileHash);
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

QByteArray DFSController::readFile(const Actor<KeyPrivate> &actor, const QString &fileHash) {
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

    const QString actorDirPathStr = makeActorDirPath(actor);
    const QString filePathStr = FileSystem::pathConcat(actorDirPathStr, fileHash);

    if (!QFileInfo::exists(filePathStr)) {
        qDebug() << "DFSController: editFile: readFile: File not found:" << filePathStr;
        return QByteArray();
    }

    QByteArray fileDecryptedContent = Utils::decryptFileIntoByteArray(filePathStr, QByteArray::fromStdString(actor.key()->secretKey()));
    return fileDecryptedContent;
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

bool DFSController::editFile(const Actor<KeyPrivate> &actor, const QString &fileHash, const QByteArray &fileContent) {
    qDebug() << "DFSController: editFile:" << fileHash;

    const std::string fileHashS = fileHash.toStdString();
    auto result = findDBRows(fileHashS);

    if (result.empty()) {
        qDebug() << "DFSController: editFile: Skipped because of empty result";
        return false;
    }

    if (result.size() > 2) {
        qDebug() << "DFSController: editFile: Query select failed: Query result has unsupported size:" << result.size();
        return false;
    }

    if (result.size() == 1 && result[0]["fileHashPrev"] == fileHashS) {
        qDebug() << "DFSController: editFile: Query select failed: fileHashPrev could not be the only field containing the fileHash:" << fileHash;
        return false;
    }

    const QString actorDirPathStr = makeActorDirPath(actor);

    //
    // Encrypt new content
    //
    const QByteArray fileContentKeccak = Utils::calcKeccak(fileContent);
    const QString fileNameNewOrigin = fileContentKeccak + "_origin";
    const QString filePathNewOrigin = FileSystem::pathConcat(actorDirPathStr, fileNameNewOrigin);
    const QString filePathNew = FileSystem::pathConcat(actorDirPathStr, fileContentKeccak);

    if (QFileInfo::exists(filePathNewOrigin)) {
        qDebug() << "DFSController: editFile: Failed: New encrypt file name already exists:" << filePathNewOrigin;
        return false;
    }

    QFile fileNewOrigin(filePathNewOrigin);
    if (!fileNewOrigin.open(QFile::WriteOnly)) {
        qDebug() << "DFSController: editFile: File opening error:" << filePathNewOrigin << fileNewOrigin.errorString();
        return false;
    }

    if (fileNewOrigin.write(fileContent) == -1) {
        qDebug() << "DFSController: editFile: File write error:" << filePathNewOrigin << fileNewOrigin.errorString();
        return false;
    }

    if (!Utils::encryptFile(filePathNew, filePathNewOrigin, QByteArray::fromStdString(actor.key()->secretKey()))) {
        qDebug() << "DFSController: editFile: Failed encryptFile:" << filePathNewOrigin << "->" << filePathNew;
        QFile().remove(filePathNew);
        QFile().remove(filePathNewOrigin);
        return false;
    }

    //
    // Remove previous content from file system
    //
    const QString filePathPrev = FileSystem::pathConcat(actorDirPathStr, QString::fromStdString(fileHashS));
    if (!QFileInfo::exists(filePathPrev)) {
        qDebug() << "DFSController: editFile: Previous file not found:" << filePathPrev;
        return true; // Nothing to delete.
    }

    QFile filePrev(filePathPrev);
    if (!filePrev.remove()) {
        qDebug() << "DFSController: editFile: Failed remove old data:" << filePathPrev << filePrev.errorString();
        return false;
    }

    //
    // Update .dir entry
    //
    auto _update = [&] (const std::string & columnName = "fileHash", const std::string & oldValue = "old_value", const std::string & newValue = "new_value") -> bool {
        const std::string & updateQuery = (std::stringstream ()
                                           << "UPDATE " << Config::DataStorage::filesTable
                                           << " SET " << columnName << " = '" << newValue
                                           << "' WHERE " << columnName << " = '" << oldValue << "'").str();
        if (!m_db.update(updateQuery)) {
            qDebug() << "DFSController: removeFile: Query update failed: columnName:" << columnName.c_str()
                     << ", oldValue:" << oldValue.c_str() << ", newValue:" << newValue.c_str()
                     << ", query:" << updateQuery.c_str();
            return false;
        }
        return true;
    };

    // If a single row returned, the fileHash column has to be modified. Happens, when the row is the first or the last in the table.
    if (!_update("fileHash", fileHashS, fileContentKeccak.toStdString())) {
        return false;
    }

    // The second row should update fileHashPrev column where previous has to be updated.
    if (result.size() == 2) {
        if (!_update("fileHashPrev", fileHashS, fileContentKeccak.toStdString())) {
            return false;
        }
    }

    return true;
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

QString DFSController::makeActorDirPath(const Actor<KeyPrivate> &actor) {
    return FileSystem::pathConcat(
                FileSystem::pathConcat(QCoreApplication::applicationDirPath(), DFSRootDirName),
                actor.id().toString());
}

QString DFSController::createDirectory(const Actor<KeyPrivate> & actor) {
    qDebug() << "DFSController: createDirectory";

    QString actorDirPath = makeActorDirPath(actor);
    if (!QDir().mkpath(actorDirPath)) {
        qDebug() << "DFSController: createDirectory: DFS actor dir create error:" << actorDirPath;
        QCoreApplication::exit(ActorDirCreateError);
    }

    return actorDirPath;
}

void DFSController::initDB(const Actor<KeyPrivate> & actor, const QString & sqliteDBTargetPath) {
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

DBRow DFSController::findDBRow(const QString &fileHash) {
    const std::string fileHashS = fileHash.toStdString();
    const std::string selectQuery = (std::stringstream()
                                     << "SELECT * FROM " << Config::DataStorage::filesTable
                                     << " WHERE fileHash = '" << fileHashS << "' OR fileHashPrev = '" << fileHashS << "'").str();
    auto result = m_db.select(selectQuery);
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
