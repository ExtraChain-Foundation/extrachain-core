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

#include "dfs/managers/headers/dfsnetmanager.h"
#include "resolve/resolve_manager.h"

void DFSNetManager::setDfs(Dfs *value)
{
    dfs = value;
}

bool DFSNetManager::isLoading(const QString &fileName)
{
    for (DFSResolverService *resolver : qAsConst(dfsResolvers))
    {
        if (resolver->getTitle().filePath == fileName)
            return true;
    }

    return false;
}

DFSNetManager::DFSNetManager(AccountController *accountList, ActorIndex *actInd, const QString &localIp)
    : NetManager(accountList, actInd, localIp)
{
    tcpPort = 2223;
    wsPort = 2234;
}

DFSNetManager::~DFSNetManager()
{
    emit finished();
    delete serverService;
}

void DFSNetManager::connectResolver(DFSResolverService *resolver)
{
    connect(resolver, &DFSResolverService::dfsTitle, this, &DFSNetManager::titleArrived);
    connect(this, &DFSNetManager::newMessage, resolver, &DFSResolverService::assignNewTask);
    connect(resolver, &DFSResolverService::TaskFinished, this, &DFSNetManager::removeResolver);
    connect(this, &DFSNetManager::newSocket, dfs, &Dfs::dfsSyncT);
}

void DFSNetManager::disconnectResolver(DFSResolverService *resolver)
{
    disconnect(resolver, &DFSResolverService::dfsTitle, this, &DFSNetManager::titleArrived);
    disconnect(this, &DFSNetManager::newMessage, resolver, &DFSResolverService::assignNewTask);
    disconnect(resolver, &DFSResolverService::TaskFinished, this, &DFSNetManager::removeResolver);
}

void DFSNetManager::createDFSResolver(Network::DataStruct ds)
{
    DFSResolverService *resolver = new DFSResolverService(Resolver::Lifetime::LONG);
    resolver->setDfs(dfs);
    resolver->setActorIndex(actorIndex);
    resolver->setTask(ds.msg, ds.receiver);
    resolver->setLongReceiver(ds.receiver);
    dfsResolvers.append(resolver);
    connectResolver(dfsResolvers.last());
    ThreadPool::addThread(dfsResolvers.last());
}

void *DFSNetManager::MessageReceived(const QByteArray &msg, const SocketPair &receiver)
{
    if (msg == Config::Net::PROTOCOL_VERSION)
    {
        qDebug() << "Protocol msg Error read";
        return nullptr;
    }
    if (!msg.isEmpty())
    {
        Network::DataStruct dStruct;
        dStruct.msg = msg;
        dStruct.receiver = receiver;
        emit newMessage(dStruct);
    }
    return nullptr;
}

void DFSNetManager::process()
{
    uResolver = new DFSResolverService(Resolver::Lifetime::SHORT);
    uResolver->setDfs(dfs);
    uResolver->setActorIndex(actorIndex);

    connectResolver(uResolver);

    ThreadPool::addThread(uResolver);
    NetManager::process();
}

void DFSNetManager::titleArrived(Network::DataStruct ds)
{
    if (dfsResolvers.size() >= DFS_RESOLVERS_POOL_SIZE)
    {
        titleVector.push(ds);
        return;
    }
    else
    {
        createDFSResolver(ds);
    }
}

void DFSNetManager::removeResolver(DFSResolverService::FinishStatus status)
{
    DFSResolverService *resolver = qobject_cast<DFSResolverService *>(QObject::sender());

    if (resolver == nullptr)
    {
        qDebug() << "WAT";
        return;
    }

    QString filePath = resolver->getTitle().filePath;
    auto pair = resolver->getLongReceiver();
    disconnectResolver(resolver);

    if (resolver->getType() == Resolver::Type::DFS)
    {
        dfsResolvers.removeOne(resolver);
    }

    if (resolver != nullptr)
        emit resolver->finished();

    if (titleVector.size() > 0)
    {
        Network::DataStruct ds = titleVector.front();
        titleVector.pop();
        createDFSResolver(ds);
    }

    switch (status)
    {
    case DFSResolverService::FinishStatus::FileReset:
        emit dfs->requestFile(filePath);
        break;
    case DFSResolverService::FinishStatus::FileFinished:
        dfs->reportFileCompleted(filePath, pair);
        break;
    case DFSResolverService::FinishStatus::FileExists:
        break;
    }
}

void DFSNetManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(tcpConnections.begin(), tcpConnections.end(),
                  [&flag](TcpSocketService *el) { flag = flag || el->isActive(); });
    std::for_each(wsConnections.begin(), wsConnections.end(),
                  [&flag](WebSocketService *el) { flag = flag || el->isActive(); });
    emit networkStatusChanged(flag);
    emit networkSocketsCountChanged(connectionsCount());

    if (flag == true)
    {
        const auto files = dfs->tmpFiles();
        for (const QString &file : files)
            emit dfs->requestFile(file);
    }
}
