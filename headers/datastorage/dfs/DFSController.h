#pragma once

#include <QObject>

#include "datastorage/actor.h"
#include "utils/db_connector.h"

class /*EXTRACHAIN_EXPORT*/ DFSController : public QObject {
    Q_OBJECT
public:
    inline static QString pathConcat(const QString& pl, const QString& pr) { return QDir::cleanPath(pl + "/" + pr); };

    DFSController(const ActorId& actorId, QObject* parent = nullptr);
    ~DFSController();

    void createDirectory();
    void initDB();
    bool addFile(const QString& filePath, const QString& fileHashPrev = "");

private:
    DBRow makeDBRow(const QString& fileHash, const QString& fileHashPrev, const QString& filePath);
    std::optional<DBRow> lastRow();
    QString lastHash();

    ActorId m_actorId;
    QString m_dfsUserDirPath;
    QString m_dfsDBFilePath;
    DBConnector m_db;
};
