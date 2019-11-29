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
#include "utils/utils.h"
#include "utils/db_connector.h"
#include <QTimer>
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
    Sender *sender;
    //    DFSResolver *resolver;
private:
    void initUserCards();
    void initDFS(const QByteArray &userId);
    void saveToDFS(const QString &path, const dfsStruct::Type &type = dfsStruct::Type::images,
                   const dfsStruct::SubType &subType = dfsStruct::SubType::ipost,
                   const dfsStruct::Status &status = dfsStruct::Status::NEW);
    void appendToCard(const QString &path, const QByteArray &userId, const dfsStruct::Type &type,
                      const dfsStruct::SubType &subType);
    QStringList returnDifs(const QString &adin, const QString &dva);
    void getDFSStatus();
    void signalConnection();
    void createNewSection(BigNumber sectionIndex, dfsStruct::Type sectionType);
    void createNewElement(BigNumber elementIndex, BigNumber sectionIndex, dfsStruct::Type sectionType);
    BigNumber getActualSection(dfsStruct::Type type);
    BigNumber getActualElementInSection(BigNumber sectiondIndex, dfsStruct::Type sectionType);
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
    void saveFN(const QString tmpPath, const QString &path, const dfsStruct::Type &type);
    void fileResponse(const QString path, const SocketPair &receiver);
    void resendFragments(QString path, QList<QByteArray> frags);
signals:
    void finished();
    void sendMsg(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver);

    void resolveMsg(const QByteArray &msg, int dMsgType, const SocketPair &receiver);
    void sendQ(const QString &filePath, const dfsStruct::Type &type, const SocketPair &receiver);
    void usersChanges(const QByteArray &path, const dfsStruct::Type &type, const QByteArray &actorId);

public slots:

    void init();
    void initUser(BigNumber userId);

    void savedNewData(const QString &path, const dfsStruct::Type &type,
                      const dfsStruct::SubType &subType = dfsStruct::SubType::ipost,
                      const dfsStruct::Status &status = dfsStruct::Status::NEW);
    void process();

private:
    QByteArray buildDfsPath(QByteArray userID, dfsStruct::Type type);
};

#endif // DFS_H
