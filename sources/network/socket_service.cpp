#include "network/socket_service.h"
#include "dfs/packages/headers/dfs_universal.h"

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

void SocketService::readData()
{
    //    while()
    while (buffer.size() > 0)
    {
        if (_blockSize == 0)
        {
            QByteArray size;

            QByteArray el = buffer.mid(0, 1);
            buffer.remove(0, 1);
            while (el != " ")
            {

                size.append(el);
                // _sok->
                el = buffer.mid(0, 1);
                buffer.remove(0, 1);
            }
            // qDebug() << "<<<<<<<<" << size;

            _blockSize = size.toInt() /*_sok->read((int)sizeof(quint16)).toInt()*/;
            // qDebug() << "_blockSize now " << _blockSize;
        }
        //        qDebug() << buffer.size() << _blockSize;
        if (buffer.size() < _blockSize)
            return;

        QByteArray command;
        command = buffer.mid(0, _blockSize);
        buffer.remove(0, _blockSize);

        if (!active)
        {
            //            active = true;
            if (command.left(IDENTIFICATOR.size()) == IDENTIFICATOR)
            {

                identificator = BigNumber(command.mid(IDENTIFICATOR.size()));
            }
            emit checkMe();
        }
        else
        {
            SocketPair receiver(address.toStdString(), port);
            receiver.setId(identificator.toByteArray());
            emit MessageReceived(command, receiver);
        }
        _blockSize = 0;
    }
};

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

SocketService::SocketService()
{
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
    buffer = value.buffer;
    reconnectTry = value.reconnectTry;
}

SocketService::SocketService(QString address, quint16 networkPort, QObject *parent)
    : QObject(parent)
{
    this->address = address;
    this->port = networkPort;
}

SocketService::SocketService(qintptr socketDescriptor, QObject *parent)
    : QObject(parent)
{
    this->socketDescriptor = socketDescriptor;
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
        QByteArray _wtSok = Serialization::universalSerialize({ data });
        socket->write(_wtSok, _wtSok.size());
    }
}

void SocketService::sockReady()
{
    //    QTcpSocket *_sok = this->socket;
    while (socket->bytesAvailable() < 4)
        socket->waitForReadyRead();
    QByteArray _sock_data = socket->read(4);
    int _pck_size = Utils::qByteArrayToInt(_sock_data);
    while (socket->bytesAvailable() < _pck_size)
        socket->waitForReadyRead();

    QByteArray pckg = socket->read(_pck_size);
    if (!active)
    {
        //            active = true;
        if (pckg.left(IDENTIFICATOR.size()) == IDENTIFICATOR)
        {

            identificator = BigNumber(pckg.mid(IDENTIFICATOR.size()));
        }
        emit checkMe();
    }
    else
    {
        SocketPair receiver(address.toStdString(), port);
        receiver.setId(identificator.toByteArray());
        emit MessageReceived(pckg, receiver);
    }
    if (socket->bytesAvailable())
        sockReady();
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
}

void SocketService::establishConnection()
{
    qDebug() << "status of socket " << this->thread() << "connection ::" << socket->isValid();
    this->address = QHostAddress(this->socket->peerAddress().toIPv4Address()).toString();
    this->port = this->socket->peerPort();
    if ((port == 2224) || (port == 2225))
        sendMsg(IDENTIFICATOR + net::dfsreadNetManagerIdentificator(),
                SocketPair(this->address.toStdString(), this->port));
    else
        this->sendMsg(IDENTIFICATOR + net::readNetManagerIdentificator(),
                      SocketPair(this->address.toStdString(), this->port));
    qDebug() << "SOCKET SERVICE: socket address " << this->socket;

    qDebug() << "SOCKET SERVICE: "
             << "socket isOpen - " << socket->isOpen();
}

void SocketService::setActive(bool active)
{
    this->active = active;
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
