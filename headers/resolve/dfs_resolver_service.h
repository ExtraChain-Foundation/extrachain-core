/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

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
#include "network/socket_pair.h"
#include "dfs/packages/headers/all.h"
#include "dfs/packages/headers/title_message.h"

class AccountController;
class ActorIndex;
class Dfs;
using namespace Resolver;

class DFSResolverService : public QObject
{
    Q_OBJECT

public:
    enum class FinishStatus
    {
        FileReset,
        FileExists,
        FileFinished
    };

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
    DistFileSystem::TitleMessage title;

private:
    bool active = false;
    std::queue<Network::DataStruct> taskQueue;
    QByteArray msg;
    QByteArray hash;
    SocketPair receiver;
    SocketPair longReceiver;
    QString path;

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
    void finishWork(FinishStatus status);
    QByteArray checkFragStatus(unsigned long from, unsigned long to);
private slots:
    void checkStatus();

private:
    /**
     * @brief validate
     * @param message
     * @return
     */
    bool validate(const Messages::BaseMessage &message);
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
    void resolveDfsMessage(QByteArray &data, const unsigned int &msgType);
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
    bool registerTitle(const QString &tmpPath, DistFileSystem::TitleMessage message);

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
    void setTask(QByteArray _msg, SocketPair _receiver);

public:
    void setDfs(Dfs *value);

    Resolver::Type getType() const;
    void setType(const Resolver::Type &value);

    Lifetime getLifetime() const;

    DistFileSystem::TitleMessage getTitle() const;
    void setLifetime(const Lifetime &value);

    void setTitle(const DistFileSystem::TitleMessage &value);

    QFile getFile() const;

    void setActorIndex(ActorIndex *value);

    SocketPair getLongReceiver() const;
    void setLongReceiver(const SocketPair &value);

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
    void TaskFinished(DFSResolverService::FinishStatus status);
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
