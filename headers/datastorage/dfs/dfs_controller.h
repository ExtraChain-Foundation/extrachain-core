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

    DFSController(QObject* parent = nullptr);
    ~DFSController();

    QByteArray addFile(const Actor<KeyPrivate> & actor, const QString & filePath, SecurityLevel securityLevel);
    bool removeFile(const Actor<KeyPrivate> & actor, const QString &fileHash, SecurityLevel securityLevel);
    QByteArray readFile(const Actor<KeyPrivate> & actor, const QString &fileHash, SecurityLevel securityLevel);
    QByteArray editFile(const Actor<KeyPrivate> & actor, const QString &fileHash, const QByteArray &fileContent,
                        SecurityLevel securityLevel, uint64_t segmentOffset = 0);
    bool flushDirContent(const Actor<KeyPrivate> & actor);
    bool initDB(const Actor<KeyPrivate> & actor);

private:
    static const QString DFSRootDirName;
    static const QString DFSDBName;
    static const QString DFSService;

    QString makeActorDirPath(const Actor<KeyPrivate> & actor);
    QString makeSecurityDirPath(const Actor<KeyPrivate> & actor, SecurityLevel securityLevel);
    QString makeServiceDirPath(const Actor<KeyPrivate> & actor);

    QString createDirectory(const Actor<KeyPrivate> & actor, SecurityLevel securityLevel);
    void initGlobalDB(const Actor<KeyPrivate> & actor, const QString & sqliteDBTargetPath);
    DBRow makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath);
    DBRow makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath,
                    const QString & fileSegmentBegin, const QString & fileSegmentEnd);
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
    QByteArray addFileSegment(const Actor<KeyPrivate> & actor, const QString & fileHash,
                        const SecurityLevel securityLevel, const QByteArray &newSegment, uint64_t newSegmentOffset);  // Place newSegment after the byteIndex pos
    QByteArray deleteFileSegment(const Actor<KeyPrivate> & actor, const QString & fileHash,
                           const SecurityLevel securityLevel, uint64_t delSegmentOffset, uint64_t segmentSize);  // Delete file content from the firstByteIndex
};

inline std::string DFSController::toStdString(DBRow &r) const {
    return "(" + r["fileHash"] + ", " + r["fileHashPrev"] + ", " + r["filePath"] + ")";
}

inline QString DFSController::toString(DBRow &r) const {
    return QString("(%1, %2, %3)").arg(r["fileHash"].c_str(), r["fileHashPrev"].c_str(), r["filePath"].c_str());
}
