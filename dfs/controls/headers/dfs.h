#ifndef DFS_H
#define DFS_H

#include "dfs/managers/headers/dfsindex.h"
#include "dfs/packages/headers/dfs_universal.h"
#include "dfs/managers/headers/card_manager.h"
#include "network/packages/service/downloaddfsrequest.h"
#include "dfs/packages/headers/ui_messages.h"
#include "dfs/packages/headers/dfs_status.h"
#include "dfs/managers/headers/package_resolver.h"
#include "dfs/packages/headers/all.h"
#include "dfs/managers/headers/sender.h"
#include <QTimer>
class Dfs : public QObject
{

    Q_OBJECT
    const QString temp_History = "history/";
    QMap<QByteArray, QString> filesQueue = {};

    QMap<QString, QByteArray> tmpFiles;

private:
    // send from nodeManger
    AccountController *accountControler;
    ActorIndex *actorIndex;
    // created here
    DfsIndex *dfsIndex;
    Sender *sender;
    DFSResolver *resolver;
    //
    void signalConnections();
    //

    /// tempquickfix
    //    void dfsSender(const QString &filePath, const SocketPair &peerAdrress);
    void initD(const QByteArray &userId);
    void saveD(const QString &path, const based_dfs_struct::Type &type = based_dfs_struct::Type::images,
               const based_dfs_struct::SubType &subType = based_dfs_struct::SubType::ipost,
               const based_dfs_struct::Status &status = based_dfs_struct::Status::NEW);
    void appendC(const QString &path, const QByteArray &userId, const QByteArray &name,
                 const QByteArray &type);
    QByteArray setName(const QByteArray &userId);
    void statusD();


public slots:
    void saveFN(const QString tmpPath, const QString &path, const based_dfs_struct::Type &type);
    void checkAc(const QByteArray &actorId, const QStringList &request, const SocketPair &receiver);
    void tmpFileControl(const long long &size, const QString &path, const QByteArray &titleS);

    void cleanTmpFile();

public:
    //    Dfs(QObject *parent = nullptr);
    Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent = nullptr);
    ~Dfs();

    DfsIndex *getDfsIndex() const;

    Sender *getSender() const;

signals:

    void newSender(const QByteArray &data, const QByteArray &msgType);
    void newSenderToPeer(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver);

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
    void sendToUser(const Messages::DfsMessage &msg, const SocketPair &receiver);

    void sendQ(const QString &path, const based_dfs_struct::Type &type, const SocketPair &receiver);

    void resolveMsg(const QByteArray &msg, int msgType, const SocketPair &receiver);

public slots:
    void savedNewData(const QString &path, const based_dfs_struct::Type &type,
                      const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status);

    //    void downloadRecieve(Messages::DownloadDfsRequestData msg, QString sender);
    //    void downloadRequset(QByteArray header, QString peerAddress);
    void init();
    void initUser(BigNumber userId);
    //
    //    void getUserDataAnswer(int request, QByteArray data);
    void receive(const QByteArray &data, const int &msgType, const SocketPair &receiver);
    void process();

    //    void checkStatus(const Messages::DfsStatus &msg);

    void appendSubscribtion(const BigNumber &actorId);

    //    void statusResponse(const QByteArray &data, const SocketPair &receiver);
};

#endif // DFS_H
