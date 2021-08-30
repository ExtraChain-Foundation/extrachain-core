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

QList<DFSResolverService *> DFSNetManager::getDfsResolvers() const
{
    return dfsResolvers;
}

DFSNetManager::DFSNetManager(AccountController *accountList, ActorIndex *actInd, const QString &localIp)
    : NetManager(accountList, actInd, localIp)
{
    tcpPort = 2225;
    wsPort = 2234;
}

DFSNetManager::~DFSNetManager()
{
    emit finished();
    delete serverService;
}

void DFSNetManager::socketConnection(TcpSocketService *socket)
{
    qDebug() << "[DFS NetworkManager] Add tcp socket connections";
    connect(socket, &TcpSocketService::clientDisconnected, this, &DFSNetManager::removeTcpConnection);
    connect(socket, &TcpSocketService::close, this, &DFSNetManager::removeTcpConnection);
    connect(socket, &TcpSocketService::checkMe, this, &DFSNetManager::checkMyIdentifier);
}

void DFSNetManager::socketDisconnect(TcpSocketService *socket)
{
    disconnect(socket, &TcpSocketService::clientDisconnected, this, &DFSNetManager::removeTcpConnection);
    disconnect(socket, &TcpSocketService::close, this, &DFSNetManager::removeTcpConnection);
    disconnect(socket, &TcpSocketService::checkMe, this, &DFSNetManager::checkMyIdentifier);
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

NetManager *DFSNetManager::getNetManager()
{
    return this->getMe();
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

void DFSNetManager::appendSocket(TcpSocketService *socket)
{
    tcpConnections.append(socket);
    socketConnection(socket);
}

void DFSNetManager::process()
{
    uResolver = new DFSResolverService(Resolver::Lifetime::SHORT);
    uResolver->setDfs(dfs);
    uResolver->setActorIndex(actorIndex);

    connectResolver(uResolver);

    ThreadPool::addThread(uResolver);
    startDFSNetwork();
}

void DFSNetManager::startDFSNetwork()
{
    startNetwork();
    // connectToNode(serverPort, local);
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

void DFSNetManager::removeTcpConnection()
{
    QObject *sender = QObject::sender();

    if (sender == nullptr)
        return;

    TcpSocketService *connection = qobject_cast<TcpSocketService *>(sender);
    socketDisconnect(connection);
    tcpConnections.removeAt(tcpConnections.indexOf(connection));
    emit connection->finished();
}

void DFSNetManager::checkMyIdentifier()
{
    QObject *sender = QObject::sender();
    TcpSocketService *connection = qobject_cast<TcpSocketService *>(sender);

    if (connection == nullptr)
        return;

    if (net::readNetManagerIdentifier() == connection->identifier())
        emit connection->close();

    // short counter = 0;
    std::for_each(tcpConnections.begin(), tcpConnections.end(), [connection](TcpSocketService *el) {
        if (el->identifier() == connection->identifier())
        {
            if (el == connection)
                emit el->setActiveSignal(true);
            else
                emit el->close();
        }
    });

    emit newSocket();
    // dfs->requestAllCards();
    // if (counter == 0)
    //    emit connection->setActiveSignal(true);
}

void DFSNetManager::addTcpConnectionFromServer(qint64 socketDescriptor)
{
    TcpSocketService *socket = new TcpSocketService(socketDescriptor);
    tcpConnections.append(socket);
    socket->setNetManager(this);
    socketConnection(socket);
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    ThreadPool::addThread(socket);
}

void DFSNetManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(tcpConnections.begin(), tcpConnections.end(),
                  [&flag](TcpSocketService *el) { flag = flag || el->getActive(); });
    emit networkStatusChanged(flag);

    if (flag == true)
    {
        const auto files = dfs->tmpFiles();
        for (const QString &file : files)
            emit dfs->requestFile(file);
    }
}

TcpSocketService *DFSNetManager::connectToTcpSocket(const QString &address, quint16 port)
{
    TcpSocketService *socket = new TcpSocketService(address, port);
    tcpConnections.append(socket);
    socket->setNetManager(this);
    socketConnection(socket);
    qDebug().noquote().nospace() << "[DFS NetworkManager] New TCP connection: " << address << ":" << port;

    ThreadPool::addThread(socket);
    QTimer::singleShot(3000, this, &DFSNetManager::checkConnectionsStatus);
    return socket;
}
