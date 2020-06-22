#include "dfs/managers/headers/dfsnetmanager.h"
#include "resolve/resolve_manager.h"

void DFSNetManager::setDfs(Dfs *value)
{
    dfs = value;
}

bool DFSNetManager::isLoading(const QString &fileName)
{
    for (DFSResolverService *resolver : dfsResolvers)
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

DFSNetManager::DFSNetManager(AccountController *accountList, ActorIndex *actInd)
    : NetManager(accountList, actInd)
{
    serverPort = isDebug ? 2225 : 2225;
}

DFSNetManager::~DFSNetManager()
{
    emit finished();
    delete serverService;
}

void DFSNetManager::socketConnection()
{
    qDebug() << "DFSNetManager connections:";
    connect(connections.last(), &SocketService::clientDisconnected, this, &DFSNetManager::removeConnection);
    connect(connections.last(), &SocketService::removeMe, this, &DFSNetManager::removeConnection);
    connect(connections.last(), &SocketService::checkMe, this, &DFSNetManager::checkMyIdentificator);
}

void DFSNetManager::socketDisconnect(SocketService *connection)
{
    disconnect(connection, &SocketService::clientDisconnected, this, &DFSNetManager::removeConnection);
    disconnect(connection, &SocketService::removeMe, this, &DFSNetManager::removeConnection);
    disconnect(connection, &SocketService::checkMe, this, &DFSNetManager::checkMyIdentificator);
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

void DFSNetManager::appendSocket(SocketService *socket)
{
    connections.append(socket);
    socketConnection();
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
    // connectToServer(serverPort, local);
}

void DFSNetManager::uiReconnect()
{
    connectToServer(serverPort, local);
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
        dfs->requestFile(filePath);
        break;
    case DFSResolverService::FinishStatus::FileFinished:
        dfs->reportFileCompleted(filePath, pair);
        break;
    case DFSResolverService::FinishStatus::FileExists:
        break;
    }
}

void DFSNetManager::removeConnection()
{
    QObject *sender = QObject::sender();

    if (sender == nullptr)
        return;

    SocketService *connection = qobject_cast<SocketService *>(sender);
    socketDisconnect(connection);
    connections.removeAt(connections.indexOf(connection));
    connection->finished();
}
void DFSNetManager::checkMyIdentificator()
{
    QObject *sender = QObject::sender();
    SocketService *connection = qobject_cast<SocketService *>(sender);

    if (connection == nullptr)
        return;

    if (allowLocalServer && net::readNetManagerIdentificator() == connection->getIdentificator())
        connection->removeMe();

    // short counter = 0;
    std::for_each(connections.begin(), connections.end(), [connection](SocketService *el) {
        if (el->getIdentificator() == connection->getIdentificator())
        {
            if (el == connection)
                emit el->setActiveSignal(true);
            else
                emit el->removeMe();
        }
    });

    emit newSocket();
    //    dfs->requestAllCards();
    // if (counter == 0)
    //    emit connection->setActiveSignal(true);
}

void DFSNetManager::addConnection(qint64 socketDescriptor)
{
    SocketService *socket = new SocketService(socketDescriptor);
    connections.append(socket);
    connections.last()->setNetManager(this);
    socketConnection();
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    ThreadPool::addThread(connections.last());
}

void DFSNetManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(connections.begin(), connections.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });
    emit qmlNetworkStatus(flag);

    if (flag == true)
    {
        const auto files = dfs->tmpFiles();
        for (const QString &file : files)
            dfs->requestFile(file);
    }
}

SocketService *DFSNetManager::addConnectionFromPair(QHostAddress address, quint16 port)
{
    SocketService *socket = new SocketService(address.toString(), port);
    connections.append(socket);
    connections.last()->setNetManager(this);
    socketConnection();
    qDebug().noquote().nospace() << "DFS NET MANAGER: New connection is established: " << address.toString()
                                 << ":" << port;

    ThreadPool::addThread(connections.last());
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return connections.last();
}
