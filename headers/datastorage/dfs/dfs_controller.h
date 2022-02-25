#pragma once

#include <QObject>

#include "datastorage/actor.h"
#include "utils/db_connector.h"

class EXTRACHAIN_EXPORT DFSController : public QObject {
    Q_OBJECT
public:
    enum DFSControllerErrors {
        RootDirCreateError = -2,
        ActorDirCreateError = -3,
        DBOpenError = -4,
        DBCreateTableError = -5,
    };

    enum SecurityLevel
    {
        Private = 0,
        Public = 1
    };

    const QStringList SecurityLevelName = { "private", "public" };


    struct AddFileMsg;
    struct RemoveFileMsg;
    struct EditFileMsg;
    struct AddSegmentMsg;
    struct DeleteSegmentMsg;

    DFSController(QObject* parent = nullptr);
    ~DFSController();

    // Internal use only
    QByteArray addFile(const Actor<KeyPrivate> & actor, const QString & filePath, SecurityLevel securityLevel);
    QByteArray readFile(const Actor<KeyPrivate> & actor, const QString &fileHash);

    // External interfaces
    QByteArray addFile(const Actor<KeyPrivate> & actor, const AddFileMsg & msg);
    bool removeFile(const Actor<KeyPrivate> & actor, const RemoveFileMsg & msg);
    QByteArray editFile(const Actor<KeyPrivate> & actor, const EditFileMsg & msg);

    bool flushDirContent(const QString & userId);
    bool initDB(const Actor<KeyPrivate> & actor);

private:
    static const QString DFSRootDirName;
    static const QString DFSDBName;
    static const QString DFSService;

    QString makeActorDirPath(const QString & actorId);
    QString makeSecurityDirPath(const Actor<KeyPrivate> & actor, SecurityLevel securityLevel);
    QString makeServiceDirPath(const Actor<KeyPrivate> & actor);
    QString makeGlobalPath(const QString & virtualPath, const QString & userId);
    SecurityLevel getSecurityLevel(const QString & virtualPath);

    bool createDirectory(const QString & path);
    void initGlobalDB(const QString & sqliteDBTargetPath);
    DBRow makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath, const QString & fileSize);
    DBRow makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath,
                    const QString & fileSegmentBegin, const QString & fileSegmentEnd, const QString & fileSize);
    DBRow findDBRow(DBConnector & db, const QString & tableName, const QString & fileHash);
    std::vector<DBRow> findDBRows(const std::string & fileHash);
    std::optional<DBRow> lastRow();
    QString lastHash();
    std::string toStdString(DBRow & r) const;
    QString toString(DBRow & r) const;

    bool setDBFieldValue(DBConnector & db,
                         const QString & tableName,
                         const QString & searchColumnTitle,
                         const QString & searchValue,
                         const QString & changeColumnTitle,
                         const QString & changeValue);

    DBConnector m_db;
    DBConnector m_db_local;

public slots:
    QByteArray addFileSegment(const Actor<KeyPrivate> & actor, const AddSegmentMsg & msg);  // Place newSegment after the byteIndex pos
    QByteArray deleteFileSegment(const Actor<KeyPrivate> &actor, const DeleteSegmentMsg & msg);  // Delete file content from the firstByteIndex
};

inline std::string DFSController::toStdString(DBRow &r) const {
    return "(" + r["fileHash"] + ", " + r["fileHashPrev"] + ", " + r["filePath"] + ")";
}

inline QString DFSController::toString(DBRow &r) const {
    return QString("(%1, %2, %3)").arg(r["fileHash"].c_str(), r["fileHashPrev"].c_str(), r["filePath"].c_str());
}


struct DFSController::AddFileMsg {
    std::string userId;
    std::string fileHash;
    std::string path;
    std::string size;

    AUTO_SERIALIZE(userId, fileHash, path, size);
};

struct DFSController::RemoveFileMsg {
    std::string userId;
    std::string fileHash;

    AUTO_SERIALIZE(userId, fileHash);
};

struct DFSController::EditFileMsg {
    std::string userId;
    std::string fileHash;
    std::string data;
    std::string offset;

    AUTO_SERIALIZE(userId, fileHash, data, offset);
};

struct DFSController::AddSegmentMsg {
    std::string userId;
    std::string fileHash;
    std::string data;
    std::string offset;

    AUTO_SERIALIZE(userId, fileHash, data, offset);
};

struct DFSController::DeleteSegmentMsg {
    std::string userId;
    std::string fileHash;
    std::string offset;
    std::string size;

    AUTO_SERIALIZE(userId, fileHash, offset, size);
};
