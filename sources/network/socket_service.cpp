#include "network/socket_service.h"
#include "dfs/packages/headers/dfs_universal.h"
#ifndef DFS_NETWORK_MANAGER_DEF
#define DFS_NETWORK_MANAGER_DEF
class DFSNetManager;
#include "dfs/managers/headers/dfsnetmanager.h"
#endif

QTcpSocket *SocketService::getSocket() const
{
    return socket;
}

void SocketService::setSocket(QTcpSocket *value)
{
    socket = value;
}

QAbstractSocket::SocketState SocketService::state()
{
    return socket->state();
}

void SocketService::reconnect()
{
    active = false;
    if (connectionTry < 3)
    {
        this->socketDescriptor = 0;
        QTimer::singleShot(4000, this, SLOT(process()));
    }
    else
    {
        this->socketDescriptor = 0;
        emit clientDisconnected();
    }
    connectionTry++;
}

int SocketService::getReconnectTry() const
{
    return reconnectTry;
}

void SocketService::setReconnectTry(int value)
{
    reconnectTry = value;
}

BigNumber SocketService::getIdentificator() const
{
    return identificator;
}

void SocketService::setIdentificator(const BigNumber &value)
{
    identificator = value;
}

bool SocketService::getActive() const
{
    return active;
}

void SocketService::setNetManager(NetManager *value)
{
    netManager = value;
}

SocketService::SocketService()
{
    dpBuffer.clear();
}

SocketService::SocketService(const SocketService &value)
{
    connectionTry = value.connectionTry;
    socketDescriptor = value.socketDescriptor;
    active = value.active;
    address = value.address;
    port = value.port;
    socket = value.socket;
    identificator = value.identificator;
    _blockSize = value._blockSize;
    //    buffer = value.buffer;
    reconnectTry = value.reconnectTry;
    dpBuffer.clear();
}

SocketService::SocketService(QString address, quint16 networkPort, QObject *parent)
//    : QObject(parent)
{
    this->address = address;
    this->port = networkPort;
    dpBuffer.clear();
}

SocketService::SocketService(qintptr socketDescriptor, QObject *parent)
//    : QObject(parent)
{
    this->socketDescriptor = socketDescriptor;
    dpBuffer.clear();
    qDebug() << "Socket Descriptor" << socketDescriptor;
}

SocketService::~SocketService()
{
    socket->close();
    socket->deleteLater();
    qDebug() << "---------> Remove SocketService" << address << port;
}

void SocketService::sendMsg(const QByteArray &data, const SocketPair &socketData)
{
    // check socket status
    if (!socket->isValid())
        return;
    // take data from pair
    QString ipAddress = QString::fromStdString(socketData.first);
    qint64 portAddress = socketData.second;
    // take socket which we need if we have 0 - port and 0.0.0.0 - ip address send anyway
    if (((ipAddress == address) || ipAddress == "0.0.0.0") && ((port == portAddress) || (portAddress == 0)))
    {
        QByteArray _wtSok = Serialization::universalSerialize({ data }, 8);
        socket->write(_wtSok, _wtSok.size());
    }
}

void *SocketService::distMsg(const QByteArray data, const SocketPair socketData)
{
    emit msgReady(data, socketData);
    QCoreApplication::processEvents();
    return nullptr;
}

void SocketService::sockReady()
{
    //    *dpBuffer = socket->readAll();
    QByteArray data = socket->readAll();
    if (data.size() + dpBuffer.size() < 8)
    {
        dpBuffer.append(data);
        return;
    }
    doRead(dpBuffer + data);
    //    dpBuffer.clear();
}

void SocketService::closeSocket()
{
    socket->disconnectFromHost();
}

void SocketService::process()
{
    if (socket == nullptr)
    {
        this->socket = new QTcpSocket(this);
        connect(socket, &QTcpSocket::connected, this, &SocketService::connected);
        connect(socket, &QTcpSocket::disconnected, this, &SocketService::reconnect);
        connect(socket, &QTcpSocket::readyRead, this, &SocketService::sockReady, Qt::QueuedConnection);
        connect(socket, &QTcpSocket::connected, this, &SocketService::establishConnection);
        connect(this, &SocketService::msgReady, this, &SocketService::sendMsg);
        connect(socket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this,
                [this](QAbstractSocket::SocketError socketError) {
                    Q_UNUSED(socketError)
                    qDebug().nospace().noquote()
                        << "Socker error " << socketError << " for " << address << ":" << port;
                    if (this->socket->state() != QTcpSocket::ConnectedState)
                        this->reconnect();
                });
        connect(this, &SocketService::setActiveSignal, this, &SocketService::setActive);
    }

    if (socketDescriptor != 0)
    {
        this->socket->setSocketDescriptor(socketDescriptor);
        establishConnection();
    }
    else
    {
        this->socket->connectToHost(address, port);
    }
    //    QCoreApplication::processEvents();
}

void SocketService::establishConnection()
{
    qDebug() << "status of socket " << this->thread() << "connection ::" << socket->isValid();
    this->address = QHostAddress(this->socket->peerAddress().toIPv4Address()).toString();
    this->port = this->socket->peerPort();

    this->distMsg(IDENTIFICATOR + net::readNetManagerIdentificator(),
                  SocketPair(this->address.toStdString(), this->port));
    qDebug() << "SOCKET SERVICE: socket address " << this->socket;

    qDebug() << "SOCKET SERVICE: "
             << "socket isOpen - " << socket->isOpen();
}

void SocketService::setActive(bool active)
{
    this->active = active;
}

void SocketService::doRead(QByteArray data)
{
    qDebug() << data;
    QByteArray msgLength = data.mid(0, 8);
    pendMsgSize = Utils::qByteArrayToInt(msgLength);

    if ((pendMsgSize != 0) && (data.size() >= pendMsgSize))
    {
        data.remove(0, 8);
        continueDoRead(data);
    }
    else
    {
        dpBuffer.append(data);
        return;
    }
}

void SocketService::continueDoRead(QByteArray data)
{
    QByteArray pckg = data.mid(0, pendMsgSize);
    data.remove(0, pendMsgSize);
    pendMsgSize = 0;
    dpBuffer = data;
    if (!this->isActive() && pckg.left(IDENTIFICATOR.size()) == IDENTIFICATOR)
    {
        QByteArray b = pckg.mid(IDENTIFICATOR.size());
        this->processID(b);
    }
    else
    {
        SocketPair receiver(this->getAddress().toStdString(), this->getPort());
        receiver.setId(this->getID().toByteArray());
        this->gotMessage(pckg, receiver);
        if (dpBuffer.size() != 0)
        {
            QByteArray d = socket->readAll();
            if (d.size() + dpBuffer.size() < 8)
            {
                dpBuffer.append(d);
                return;
            }
            doRead(dpBuffer + d);
        }
        //        doRead(data);
    }
}

void SocketService::gotMessage(QByteArray msg, SocketPair rec)
{
    if (socket->localPort() == 2223 || socket->localPort() == 2224)
    {
        reinterpret_cast<DFSNetManager *>(netManager)->MessageReceived(msg, rec);
    }
    else
        netManager->MessageReceived(msg, rec);
}

BigNumber SocketService::getID()
{
    return identificator;
}

void SocketService::processID(QByteArray id)
{
    identificator = BigNumber(id);
    emit checkMe();
}

bool *SocketService::socketStatus() const
{
    return new bool(socket->isValid());
}

bool SocketService::isActive() const
{
    return active;
}

QString SocketService::getAddress() const
{
    return address;
}

quint16 SocketService::getPort() const
{
    return port;
}

QHostAddress SocketService::getSocketAddress() const
{
    return socket->peerAddress();
}

quint16 SocketService::getSocketPeer() const
{
    return socket->peerPort();
}
