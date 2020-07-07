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

#ifndef RESOLVE_MANAGER_H
#define RESOLVE_MANAGER_H

#ifndef NETWORK_MANAGER_DEF
#define NETWORK_MANAGER_DEF
class NetManager;
#include "network/network_manager.h"
#endif
#ifndef RESOLVER_SERVICE_DEF
#define RESOLVER_SERVICE_DEF
class ResolverService;
#include "resolve/resolver_service.h"
#endif
#ifndef NODE_MANAGER_DEF
#define NODE_MANAGER_DEF
class NodeManager;
#include "managers/node_manager.h"
#endif

#include "managers/chatmanager.h"
class ChatManager;

#include <QObject>
//#include <QQueue>
#include <queue>
#include <vector>
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/tx_manager.h"
#include "dfs/controls/headers/dfs.h"

#ifdef ECONSOLE
static const short ResolverServicePoolMaxSize = 100;
#endif

#ifdef ECLIENT
static const short ResolverServicePoolMaxSize = 5;
#endif

class ResolveManager : public QObject
{
    Q_OBJECT

private:
    std::queue<Network::DataStruct> unprocessed;
    QList<ResolverService *> l1Res;
    QMap<QByteArray, int> *requestResponseMap = new QMap<QByteArray, int>();
    //    QMap<QByteArray, unsigned long> *pckgCounter = new QMap<QByteArray, unsigned long>();

private:
    ActorIndex *actorIndex;
    Blockchain *blockchain;
    NetManager *networkManager;
    TransactionManager *txManager;
    AccountController *accountControler;
    NodeManager *node;
    ChatManager *chatManager;

public:
    ResolveManager(ActorIndex *actorIndex, Blockchain *blockchain, NetManager *networkManager,
                   TransactionManager *txManager, AccountController *accountControler,
                   QObject *parent = nullptr);
    ~ResolveManager();

    void setNode(NodeManager *value);

private:
    void connectSignals(ResolverService *resolver);
    void disconnectSignals(ResolverService *resolver);

    const QByteArray calcKeccak256(const QByteArray &msg) const;
    /**
     * @brief createNewResolver
     * @param task
     */
    void createNewResolver(const Network::DataStruct &task);

private:
    QList<ResolverService *> getActive();
    QList<ResolverService *> getFinished();
    bool popUnprocces();

public:
    bool setTask(QByteArray msg, const SocketPair &receiver);
    void setChatManager(ChatManager *value);

    QMap<QByteArray, std::vector<bool>> *getDataCheckers() const;

    //    QMap<QByteArray, unsigned long> *getPckgCounter() const;

    QMap<QByteArray, QString> *getDownloadingFileList() const;

    QMap<QByteArray, QFile *> *getListFile() const;

    QMap<QByteArray, int> *getRequestResponseMap() const;

    QMap<QString, QByteArray> *getFileMap() const;

signals:
    void finished();
    //    void coinRequest(BigNumber id, BigNumber amount);
    //    void sendMsg(const QByteArray &msg);
    void socketSendMsg(const QByteArray &serialized, const SocketPair &receiver);
public slots:
    //    void resolveMessage(const QByteArray &msg, const SocketPair &receiver);
    void registrateMsg(const QByteArray &data, const unsigned int &msgType);
    /**
     * @brief sendMessageResponse from resolver
     * @param data
     * @param msgType
     * @param requestHash
     * @param receiver
     */
    void sendMessageResponse(const QByteArray &data, const unsigned int &msgType,
                             const QByteArray &requestHash, const SocketPair &receiver);
    void taskFinished();
public slots:
    void process();
};

#endif // RESOLVE_MANAGER_H
