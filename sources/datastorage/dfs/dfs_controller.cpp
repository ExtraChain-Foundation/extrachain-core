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

QString DFSController::createDirectory(const Actor<KeyPrivate> & actor) {
    qDebug() << "DFSController: createDirectory";

    QString dfsRootDirPath = FileSystem::createSubDirectory(QCoreApplication::applicationDirPath(), DFSRootDirName);
    if (dfsRootDirPath.isEmpty()) {
        qDebug() << "DFSController: createDirectory: DFS root dir create error:" << dfsRootDirPath;
        QCoreApplication::exit(RootDirCreateError);
    }

    QString actorDirPath = FileSystem::createSubDirectory(dfsRootDirPath, actor.id().toString());
    if (actorDirPath.isEmpty()) {
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
