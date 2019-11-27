#include "dfsnetmanager.h"
#include "headers/resolve/resolve_manager.h"

DFSNetManager::DFSNetManager(AccountController *accountList, ActorIndex *actInd)
    : NetManager(accountList, actInd)
{
    serverPort = isDebug ? 2224 : 2225;
}

DFSNetManager::~DFSNetManager()
{
    emit finished();
    delete serverService;
}

void DFSNetManager::socketConnection()
{
    qDebug() << "DFSNetManager connections:";
    qDebug() << connect(socketsList.last(), &SocketService::clientDisconnected, this,
                        &DFSNetManager::removeConnection);
    //    qDebug() << connect(this, &DFSNetManager::sendMsg, socketsList.last(), &SocketService::sendMsg);
    //    qDebug() << connect(socketsList.last(), &SocketService::MessageReceived, this,
    //    &DFSNetManager::newMsg);
    qDebug() << connect(socketsList.last(), &SocketService::removeMe, this, &DFSNetManager::removeConnection);
    qDebug() << connect(socketsList.last(), &SocketService::checkMe, this,
                        &DFSNetManager::checkMyIdentificator);
}

void DFSNetManager::socketDisconnect(SocketService *connection)
{
    disconnect(connection, &SocketService::clientDisconnected, this, &DFSNetManager::removeConnection);
    //    disconnect(this, &DFSNetManager::sendMsg, connection, &SocketService::sendMsg);
    //    disconnect(connection, &SocketService::MessageReceived, this, &DFSNetManager::newMsg);
    disconnect(connection, &SocketService::removeMe, this, &DFSNetManager::removeConnection);
    disconnect(connection, &SocketService::checkMe, this, &DFSNetManager::checkMyIdentificator);
}

void DFSNetManager::startNetwork()
{
    qDebug() << "DFSNetManager::startNetwork()";
    //        netPort = serverPort;
    qDebug() << "DFSNetManager:" << serverPort;

    if (local != nullptr)
    {
        serverService = new ServerService(serverPort, local);
        //    resolverService = new ResolverService(actorIndex, requestResponseMap);
        setupServerServiceConnections();
        serverService->startListen();
    }
}

void DFSNetManager::setupServerServiceConnections()
{
    connect(serverService, &ServerService::newConnection, this, &DFSNetManager::addConnection,
            Qt::UniqueConnection);
#ifdef ETALONIUM_CLIENT
    connect(serverService, &ServerService::serverStatus, this, &NetManager::qmlServerError);
#endif
}

NetManager *DFSNetManager::getNetManager()
{
    return this->getMe();
}

void *DFSNetManager::MessageReceived(const QByteArray &msg, const SocketPair &receiver)
{
    mutex.lock();
    //    if (checkMsgCount(msg, handler, socketsList))
    resolveManager->setTask(msg, receiver);
    //    else qDebug()
    //        << "[&DFSNetManager]::checkMsgCount have returned false ~ such message has been already added";
    mutex.unlock();
    return nullptr;
}

void DFSNetManager::appendSocket(SocketService *socket)
{
    socketsList.append(socket);
    socketConnection();
}

// void DFSNetManager::newMsg(const QByteArray &message, const SocketPair &receiver)
//{
//    if (checkMsgCount(message, handler))
//        resolveManager->setTask(message, receiver);
//    else
//        qDebug()
//            << "[&DFSNetManager]::checkMsgCount have returned false ~ such message has been already added";
//}

void DFSNetManager::send(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver)
{
    Messages::BaseMessage msg(msgType);
    msg.init(data);
    //    if (msgType != Messages::ACTOR_MESSAGE)
    //        msg.calcDigSig(accounts->getCurrentActor());

    QByteArray message = msg.serialize();
    //    if (checkMsgCount(message, handler, socketsList))
    //    {
    std::for_each(socketsList.begin(), socketsList.end(),
                  [&message, &receiver](SocketService *socket) { socket->distMsg(message, receiver); });
    //        qDebug() << "emit sendM from DFSNetManager";
    //    }
}

void DFSNetManager::process()
{
    startNetwork();
    connectToServer(serverPort, local);
}

void DFSNetManager::uiReconnect()
{
    connectToServer(serverPort, local);
}

void DFSNetManager::removeConnection()
{
    QObject *sender = QObject::sender();
    SocketService *connection = qobject_cast<SocketService *>(sender);
    socketDisconnect(connection);
    socketsList.removeAt(socketsList.indexOf(connection));
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
    std::for_each(socketsList.begin(), socketsList.end(), [connection](SocketService *el) {
        if (el->getIdentificator() == connection->getIdentificator())
        {
            if (el == connection)
                emit el->setActiveSignal(true);
            else
                emit el->removeMe();
        }
    });
    // if (counter == 0)
    //    emit connection->setActiveSignal(true);
}

void DFSNetManager::addConnection(qint64 socketDescriptor)
{
    SocketService *socket = new SocketService(socketDescriptor);
    socketsList.append(socket);
    socketsList.last()->setNetManager(this);
    socketConnection();
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    ThreadPool::addThread(socketsList.last());
}

void DFSNetManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(socketsList.begin(), socketsList.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });
    emit qmlNetworkStatus(flag);
}

void DFSNetManager::connectToServer(const quint16 &serverPort, QNetworkAddressEntry *local)
{
#ifdef ETALONIUM_CONSOLE
    return;
#endif
    qDebug() << "void NetManager::connectToServer()";
    QStringList servers = serverIp.split(";");
    QString localIp = local != nullptr ? local->ip().toString() : "";

    for (QString server : servers)
    {
        server = server.trimmed();
        if (server.isEmpty())
            continue;

        quint16 port = serverPort;
        bool customPort = server.indexOf(":") != -1;

        if (customPort)
        {
            QStringList serverAndPort = server.split(":");
            server = serverAndPort[0].trimmed();
            port = quint16(serverAndPort[1].trimmed().toUInt());
        }

        if (server != localIp || allowLocalServer)
        {
            qDebug().noquote() << QString("Server: try connect to %1:%2").arg(server).arg(port);
            addConnectionFromPair(QHostAddress(server), port);
        }
        else
        {
            qDebug().noquote() << QString("Server: ignore %1:%2").arg(server).arg(port);
        }
    }
}

SocketService *DFSNetManager::addConnectionFromPair(QHostAddress address, quint16 port)
{
    SocketService *socket = new SocketService(address.toString(), port);
    socketsList.append(socket);
    socketsList.last()->setNetManager(this);
    socketConnection();
    qDebug() << "NET MANAGER: New connection is established : " << address << ":" << port;

    ThreadPool::addThread(socketsList.last());
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return socketsList.last();
}
