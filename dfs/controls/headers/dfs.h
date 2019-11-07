#ifndef DFS_H
#define DFS_H

#include "dfs/managers/headers/dfsindex.h"
#include "dfs/packages/headers/dfs_universal.h"
#include "dfs/managers/headers/card_manager.h"
#include "network/packages/service/downloaddfsrequest.h"
#include "dfs/packages/headers/ui_messages.h"
#include "dfs/packages/headers/dfs_status.h"
//#include "dfs/managers/headers/package_resolver.h"
#include "dfs/packages/headers/all.h"
#include "dfs/managers/headers/sender.h"
#include "dfs/managers/headers/dfsnetmanager.h"
#include <QTimer>
class Dfs : public QObject
{

    Q_OBJECT

private:
    // send from nodeManger
    AccountController *accountControler;
    ActorIndex *actorIndex;

    DFSNetManager *dfsNetManager;
    Sender *sender;
    //    DFSResolver *resolver;

    void initD(const QByteArray &userId);
    void saveD(const QString &path, const based_dfs_struct::Type &type = based_dfs_struct::Type::images,
               const based_dfs_struct::SubType &subType = based_dfs_struct::SubType::ipost,
               const based_dfs_struct::Status &status = based_dfs_struct::Status::NEW);
    void appendC(const QString &path, const QByteArray &userId, const QByteArray &name,
                 const QByteArray &type);
    QByteArray setName(const QByteArray &userId);
    void statusD();
    void signalConnection();

public slots:

    void checkAc(const QByteArray &actorId, const QStringList &request, const SocketPair &receiver);

public:
    Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent = nullptr);
    ~Dfs();

public:
    void initDFSNetManager(ResolveManager *resolveManager);
    DFSNetManager *getDfsNetManager() const;
    void setDfsNetManager(DFSNetManager *value);
    void saveFN(const QString tmpPath, const QString &path, const based_dfs_struct::Type &type);

signals:
    void finished();
    void sendMsg(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver);

    void resolveMsg(const QByteArray &msg, int dMsgType, const SocketPair &receiver);
    void sendQ(const QString &filePath, const based_dfs_struct::Type &type, const SocketPair &receiver);
    void usersChanges(const QByteArray &path, const based_dfs_struct::Type &type, const QByteArray &actorId);

public slots:

    void init();
    void initUser(BigNumber userId);

    void savedNewData(const QString &path, const based_dfs_struct::Type &type,
                      const based_dfs_struct::SubType &subType = based_dfs_struct::SubType::ipost,
                      const based_dfs_struct::Status &status = based_dfs_struct::Status::NEW);
    void process();
};

#endif // DFS_H
