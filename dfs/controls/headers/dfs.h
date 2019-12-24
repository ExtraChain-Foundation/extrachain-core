#ifndef DFS_H
#define DFS_H

#include "dfs/managers/headers/dfsindex.h"
#include "dfs/packages/headers/dfs_universal.h"
#include "dfs/managers/headers/card_manager.h"
#include "network/packages/service/downloaddfsrequest.h"
#include "dfs/packages/headers/ui_messages.h"
#include "dfs/packages/headers/dfs_status.h"
#include "dfs/packages/headers/dfs_changes.h"
//#include "dfs/managers/headers/package_resolver.h"
#include "dfs/packages/headers/all.h"
#include "dfs/managers/headers/sender.h"
#include "dfs/managers/headers/dfsnetmanager.h"
#include "utils/utils.h"
#include "utils/db_connector.h"
#include "dfs/controls/headers/subscribe_controller.h"
#include <QTimer>
#include <QDirIterator>
#include <iterator>
#ifdef ETALONIUM_CLIENT
#include <QImage>
#endif

class Dfs : public QObject
{

    Q_OBJECT

private:
    // send from nodeManger
    AccountController *accountControler;
    ActorIndex *actorIndex;
    DBConnector uCards;
    Sender *sender = nullptr;
    // DFSResolver *resolver;

private:
    void initDFS(const QByteArray &userId);
    void saveToDFS(const QString &path, const QByteArray &data,
                   const DfsStruct::Type &type = DfsStruct::Type::images,
                   const DfsStruct::SubType &subType = DfsStruct::SubType::subpost);
    void saveStaticFile(QString fileName, DfsStruct::Type type, DfsStruct::SubType subType);
    void saveFN(const QString tmpPath, const QString &path, const DfsStruct::Type &type);
    bool appendToCard(const QString &path, const QByteArray &userId, const DfsStruct::Type &type,
                      const DfsStruct::SubType &subType = DfsStruct::SubType::undef);
    QStringList returnDiffs(const QString &odin, const QString &odinson);
    void getDFSStatus();
    void signalConnection();

public slots:
    void checkAc(const QByteArray &actorId, const QStringList &request, const SocketPair &receiver);

public:
    DFSNetManager *dfsNetManager;
    Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent = nullptr);
    ~Dfs();

public:
    void initDFSNetManager(ResolveManager *resolveManager);
    DFSNetManager *getDfsNetManager() const;
    void setDfsNetManager(DFSNetManager *value);
    void fileResponse(const QString filePath, const SocketPair &receiver);
    void sendFragments(QString path, QByteArray frags, SocketPair receiver);
    Sender *getSender() const;

    QStringList tmpFiles() const;

signals:
    void finished();
    void sendMsg(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver);

    void resolveMsg(const QByteArray &msg, int dMsgType, const SocketPair &receiver);
    void sendQ(const QString &filePath, const DfsStruct::Type &type, const SocketPair &receiver);
    void usersChanges(const QByteArray &path, const DfsStruct::Type &type, const QByteArray &actorId);
    void fileChanged(QString path);
    void sendFromNetwork(int saveType, QString file, QByteArray data, const DfsStruct::Type type,
                         const DfsStruct::SubType subType = DfsStruct::SubType::subpost);

public slots:
    void init();
    void initUser(BigNumber userId);

    void save(int saveType, QString file, QByteArray data, const DfsStruct::Type type,
              const DfsStruct::SubType subType = DfsStruct::SubType::subpost);
    void editData(QString userId, QString fileName, DfsStruct::Type type, QByteArray data);
    void editSqlDatabase(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                         QByteArrayList sqlChanges);
    bool applyChanges(const DFSMessage::DfsChanges &dfsChanges);
    // void appendData(QString userId, QString fileName, QByteArray data);
    void process();
    void requestFile(const QString &filePath);
    void searchTmp(bool reqFile = false);

private:
    QByteArray buildDfsPath(QByteArray userID, DfsStruct::Type type);
    bool createStored(QString filePath, const QByteArray &userId, const DfsStruct::Type &type);
    bool appendToStored(QString filePath, QByteArray data, QString range, int type, QString userId, bool init,
                        QByteArray hash);
    void updateFromNewStored(QString filePath);
    bool updateCard(const QString &path, const QByteArray &userId, QByteArray date, QByteArray newHash);
    bool applyChangesBytes(const DFSMessage::DfsChanges &dfsChanges);
    bool applyChangesSql(const DFSMessage::DfsChanges &dfsChanges);
    DfsStruct::Type getFileType(const QString &filePath);

    QTimer *timerTmpFiles;
    QStringList m_tmpFiles;
};

#endif // DFS_H
