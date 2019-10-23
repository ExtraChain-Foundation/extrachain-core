#ifndef DFS_H
#define DFS_H

#include "dfs/managers/headers/dfsindex.h"
#include "dfs/packages/headers/dfs_universal.h"
#include "dfs/managers/headers/card_manager.h"
#include "network/packages/service/downloaddfsrequest.h"
#include "dfs/packages/headers/ui_messages.h"
#include "dfs/packages/headers/dfs_status.h"
#include "dfs/managers/headers/package_resolver.h"

class Dfs : public QObject
{
    Q_OBJECT
    const QString temp_History = "history/";
    QMap<QByteArray, QString> filesQueue = {};

private:
    // send from nodeManger
    AccountController *accountControler;
    ActorIndex *actorIndex;
    // created here
    DfsIndex *dfsIndex;
    //
    void signalConnections();
    //

    /// tempquickfix

    void initD(const QByteArray &userId);
    void saveD(const QString &path, const based_dfs_struct::Type &type = based_dfs_struct::Type::images,
               const based_dfs_struct::SubType &subType = based_dfs_struct::SubType::ipost,
               const based_dfs_struct::Status &status = based_dfs_struct::Status::NEW);
    void appendC(const QString &path, const QByteArray &userId, const QByteArray &name,
                 const QByteArray &type);
    QByteArray setName(const QByteArray &userId);

public:
    //    Dfs(QObject *parent = nullptr);
    Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent = nullptr);
    ~Dfs();

    DfsIndex *getDfsIndex() const;

signals:

    void newSender(const QByteArray &data, const QByteArray &msgType);

    void construction();
    // changes from/to dfs
    void newChanges(Messages::DfsMessage data);
    void usersChanges(QByteArray data, based_dfs_struct::Type type, BigNumber actorId);
    //
    //    void sendRequests(int request, QByteArray data);
    void sendChanges(Messages::DfsMessage data, QString peerAddress);
    //
    void downloadResponse(bool, QByteArray header, QString peerAddress);
    void beginTest();
    void finished();

    void sendMessage(const Messages::DfsMessage &msg);
    void sendToPeer(const Messages::DfsMessage &msg, const QString &peerAddress);
    void sendDfsStatus(const Messages::DfsStatus &status);

public slots:
    void savedNewData(const QString &path, const based_dfs_struct::Type &type,
                      const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status);

    //    void downloadRecieve(Messages::DownloadDfsRequestData msg, QString sender);
    void downloadRequset(QByteArray header, QString peerAddress);
    void init();
    void initUser(BigNumber userId);
    //
    void getUserDataAnswer(int request, QByteArray data);
    void recieve(Messages::DfsMessage msg);
    void process();

    //    void checkStatus(const Messages::DfsStatus &msg);

    void resolveMsg(const Messages::DfsMessage &msg);

    void appendSubscribtion(const BigNumber &actorId);

    void statusResponse();
};

#endif // DFS_H
