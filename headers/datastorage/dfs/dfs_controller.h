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

    DFSController(QObject* parent = nullptr);
    ~DFSController();

    QByteArray addFile(const Actor<KeyPrivate> & actor, const QString & filePath);
    bool removeFile(const Actor<KeyPrivate> & actor, const QString &fileHash);
    bool flushDirContent(const Actor<KeyPrivate> & actor);

private:
    static const QString DFSRootDirName;
    static const QString DFSDBName;

    QString makeActorDirPath(const Actor<KeyPrivate> & actor);
    QString createDirectory(const Actor<KeyPrivate> & actor);
    void initDB(const Actor<KeyPrivate> & actor, const QString & sqliteDBTargetPath);
    DBRow makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath);
    std::optional<DBRow> lastRow();
    QString lastHash();
    std::string toStdString(DBRow & r) const;
    QString toString(DBRow & r) const;

    DBConnector m_db;
};

inline std::string DFSController::toStdString(DBRow &r) const {
    return "(" + r["fileHash"] + ", " + r["fileHashPrev"] + ", " + r["filePath"] + ")";
}

inline QString DFSController::toString(DBRow &r) const {
    return QString("(%1, %2, %3)").arg(r["fileHash"].c_str(), r["fileHashPrev"].c_str(), r["filePath"].c_str());
}
