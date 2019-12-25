#ifndef DFS_RESOVLER_SERVICE_H
#define DFS_RESOVLER_SERVICE_H
#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QTimer>
#include <QMap>
#include <vector>
#include <queue>
#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "network/packages/base_message.h"
#include "network/packages/base_message_response.h"
#include "network/packages/service/response_messages.h"
#include "network/socket_pair.h"
#include "dfs/packages/headers/all.h"
class AccountController;
class ActorIndex;
class Dfs;
using namespace Resolver;

class DFSResolverService : public QObject
{
    Q_OBJECT
private:
    ActorIndex *actorIndex;
    Dfs *dfs;

private:
    Resolver::Type type = Resolver::Type::DFS;
    Resolver::Lifetime lifetime = Resolver::Lifetime::SHORT;

private:
    unsigned long reqStart = 0;
    unsigned long reqFin = 0;
    QTimer *reloadTimer = nullptr;
    //    QByteArray tag;
    std::vector<bool> dataChecker;
    //    QString path;
    QFile file;
    DFSMessage::title_message title;

private:
    bool active = false;
    std::queue<Network::DataStruct> taskQueue;
    QByteArray msg;
    QByteArray hash;
    SocketPair receiver;

public:
    /**
     * @brief ResolverService
     * @param actorIndex
     * @param parent
     */
    DFSResolverService(Resolver::Lifetime lifetime, QObject *parent = nullptr);
    /**
     * @brief ResolverService
     */
    ~DFSResolverService() override;

private:
    void finishWork();
    QByteArray checkFragStatus(unsigned long from, unsigned long to);
private slots:
    void checkStatus();

private:
    /**
     * @brief validate
     * @param message
     * @return
     */
    bool validate(const Messages::IMessage &message);
    bool MessageIsNotValid(const Messages::IMessage &message);
    /**
     * @brief addResponseHandler
     * @param message
     * @return
     */
    void resolveDfsTask();
    /**
     * @brief resolveDfsMessage
     * @param data
     * @param msgType
     * @param receiver
     */
    void resolveDfsMessage(const QByteArray &data, const int &msgType);
    /**
     * @brief createTempFile
     * @param path
     * @param size
     * @param tHash
     * @return
     */
    bool createTempFile(const QString &path, const long long &size, const QByteArray &tHash);
    /**
     * @brief registerTitle
     * @param tmpPath
     * @param pckgAmount
     * @param size
     * @param titleSerialize
     * @param tHash
     * @return
     */
    bool registerTitle(const QString &tmpPath, DFSMessage::title_message title);

public:
    /**
     * @brief isActive
     * @return
     */
    bool isActive() const;
    /**
     * @brief setTask
     * @param msg
     * @param receiver
     */
    void setTask(QByteArray msg, SocketPair receiver);

public:
    void setDfs(Dfs *value);

    Resolver::Type getType() const;
    void setType(const Resolver::Type &value);

    Lifetime getLifetime() const;

    DFSMessage::title_message getTitle() const;
    void setLifetime(const Lifetime &value);

    void setTitle(const DFSMessage::title_message &value);

    QFile getFile() const;

public slots:
    /**
     * @brief process
     * slot for threadpool
     * ready for work
     */
    void process();
    void assignNewTask(Network::DataStruct task);

signals:
    void dfsTitle(Network::DataStruct ds);
    /**
     * @brief TaskFinished signal to resolver manager
     * the work have been finished you could kill me
     */
    void TaskFinished();
    /**
     * @brief responseReady to network manager
     * @param data
     * @param msgType
     * @param requestHash
     * @param receiver
     */
    // signal for thread pool
    void finished();
};
#endif // DFS_RESOVLER_SERVICE_H
