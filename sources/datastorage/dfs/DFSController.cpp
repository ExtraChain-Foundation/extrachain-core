#include "datastorage/dfs/DFSController.h"

#include "utils/exc_utils.h"

#include <QDir>
#include <QFileInfo>

#include <QDebug>

static const QString DfsRootDirName = "dfs";
static const QString DfsDBName = "dfs_db.sqlite3";
static const std::string DfsDBTableName = "files_table";

DFSController::DFSController(const ActorId &actorId, QObject* parent)
    : QObject(parent)
    , m_actorId(actorId)
    , m_dfsUserDirPath("")
    , m_dfsDBFilePath("")
    , m_db()
{
    createDirectory();
    initDB();

    const QString fn1 = QDir::homePath() + "/test-file-1.txt";
    QString lh = lastHash();
    addFile(fn1, lh);

    const QString fn2 = QDir::homePath() + "/test-file-2.jpg";
    lh = lastHash();
    addFile(fn2, lh);
}

DFSController::~DFSController() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void DFSController::createDirectory() {
    qDebug() << "DFSController: createDirectory";

    auto createSubDirectory = [] (const QString & parentDirStr, const QString & subDirStr) {
        QString destPathStr = QDir::cleanPath(parentDirStr + "/" + subDirStr);
        QDir parentDir(parentDirStr);
        if (!parentDir.exists(subDirStr)) {
            if (!parentDir.mkdir(subDirStr)) {
                destPathStr = "";
            }
        }
        return destPathStr;
    };

    QString dfsRootDirPath = createSubDirectory(QDir::homePath(), DfsRootDirName);
    if (dfsRootDirPath.isEmpty()) {
        qDebug() << "DFSController: createDirectory: DFS root dir create error:" << dfsRootDirPath;
        QCoreApplication::exit(-2);
    }

    m_dfsUserDirPath = createSubDirectory(dfsRootDirPath, m_actorId.toString());
    if (m_dfsUserDirPath.isEmpty()) {
        qDebug() << "DFSController: createDirectory: DFS user dir create error:" << m_dfsUserDirPath;
        QCoreApplication::exit(-3);
    }
}

void DFSController::initDB() {
    qDebug() << "DFSController: initDB";

    m_dfsDBFilePath = QDir::cleanPath(m_dfsUserDirPath + "/" + DfsDBName);
    const bool dbExists = QFileInfo::exists(m_dfsDBFilePath);

    if (!m_db.open(m_dfsDBFilePath.toStdString())) {
        qDebug() << "DFSController: initDB: Can't open DB:" << m_dfsDBFilePath;
        QCoreApplication::exit(-4);
    }

    if (!dbExists) {
        qDebug() << "DFSController: initDB: Create table:" << Config::DataStorage::filesTable.c_str();
        if (!m_db.createTable(Config::DataStorage::filesTableCreate)) {
            qDebug() << "DFSController: initDB: Create table failed: " << m_dfsDBFilePath << Config::DataStorage::filesTable.c_str();
            QCoreApplication::exit(-5);
        }
    }
}

bool DFSController::addFile(const QString &filePath, const QString &fileHashPrev) {
    if (!m_db.isOpen()) {
        qDebug() << "DFSController: addFile: Failed, DB is not open: " << m_dfsDBFilePath;
        return false;
    }

    const QByteArray fileKey = "TODO_FIND_OUT_HOW_TO";
    const QString fileExt = QFileInfo(filePath).suffix();
    const QByteArray fileKeccak = Utils::calcKeccakForFile(filePath);
    const QString destFilePath = pathConcat(m_dfsUserDirPath, fileKeccak) + "." + fileExt;
    bool fileEnctrypted = Utils::encryptFile(filePath, destFilePath, fileKey);
    if (!fileEnctrypted) {
        qDebug() << "DFSController: addFile: Failed encryptFile: " << filePath;
        return false;
    }

    const auto rowData = makeDBRow(fileKeccak, fileHashPrev, destFilePath);
    if (!m_db.insert(Config::DataStorage::filesTable, rowData)) {
        qDebug() << "DFSController: addFile: insert failed: " << m_dfsDBFilePath << " : " << Config::DataStorage::filesTable.c_str();
    }

    return fileEnctrypted;
}

DBRow DFSController::makeDBRow(const QString &fileHash, const QString &fileHashPrev, const QString &filePath) {
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
