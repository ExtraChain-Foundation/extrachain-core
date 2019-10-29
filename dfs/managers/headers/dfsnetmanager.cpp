#include "dfsnetmanager.h"

void DFSNetManager::socketConnection()
{
    connect(socketsList.last(), &SocketService::clientDisconnected, this, &DFSNetManager::removeConnection);
    connect(this, &DFSNetManager::sendMsg, socketsList.last(), &SocketService::sendMsg);
    connect(socketsList.last(), &SocketService::MessageReceived, this, &DFSNetManager::newMsg);
    connect(socketsList.last(), &SocketService::removeMe, this, &DFSNetManager::removeConnection);
    connect(socketsList.last(), &SocketService::checkMe, this, &DFSNetManager::checkMyIdentificator);
}

void DFSNetManager::socketDisconnect(SocketService *connection)
{
    disconnect(connection, &SocketService::clientDisconnected, this, &DFSNetManager::removeConnection);
    disconnect(this, &DFSNetManager::sendMsg, connection, &SocketService::sendMsg);
    disconnect(connection, &SocketService::MessageReceived, this, &DFSNetManager::newMsg);
    disconnect(connection, &SocketService::removeMe, this, &DFSNetManager::removeConnection);
    disconnect(connection, &SocketService::checkMe, this, &DFSNetManager::checkMyIdentificator);
}

DFSNetManager::DFSNetManager(AccountController *accountList, ActorIndex *actInd)
    : NetManager(accountList, actInd)
{
    serverIp = "51.68.181.52";
    serverPort = isDebug ? 2224 : 2225;
#ifdef ETALONIUM_CLIENT
    QSettings settings;

    if (!settings.value("network/serverIp").isValid())
        settings.setValue("network/serverIp", "51.68.181.52");
    if (!settings.value("network/allowLocalServer").isValid())
        settings.setValue("network/allowLocalServer", "false");

    serverIp = settings.value("network/serverIp").toString();
    allowLocalServer = settings.value("network/allowLocalServer").toBool();
#endif
}

DFSNetManager::~DFSNetManager()
{
    emit finished();
}

NetManager *DFSNetManager::getNetManager()
{
    return this->getMe();
}

void DFSNetManager::appendSocket(SocketService *socket)
{
    socketsList.append(socket);
    socketConnection();
}

void DFSNetManager::newMsg(const QByteArray &message, const SocketPair &receiver)
{
    if (checkMsgCount(message, handler))
        emit newMessage(message, receiver);
    else
        qDebug()
            << "[&DFSNetManager]::checkMsgCount have returned false ~ such message has been already added";
}

void DFSNetManager::send(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver)
{
    Messages::BaseMessage msg(msgType);
    msg.init(data);
    if (msgType != Messages::ACTOR_MESSAGE)
        msg.calcDigSig(accounts->getCurrentActor());

    //    qDebug() << "NetManager: send " << msgType;
    QByteArray message = msg.serialize();
    if (checkMsgCount(message, handler))
        emit sendMsg(message, receiver);
}

void DFSNetManager::process()
{
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
