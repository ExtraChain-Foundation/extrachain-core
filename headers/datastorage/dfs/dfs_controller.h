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

private:
    static const QString DFSRootDirName;
    static const QString DFSDBName;

    QString createDirectory(const Actor<KeyPrivate> & actor);
    void initDB(const Actor<KeyPrivate> & actor, const QString & sqliteDBTargetPath);
    DBRow makeDBRow(const QString & fileHash, const QString & fileHashPrev, const QString & filePath);
    std::optional<DBRow> lastRow();
    QString lastHash();

    DBConnector m_db;
};
