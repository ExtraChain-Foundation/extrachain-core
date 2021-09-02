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

#include "network/tcpsocket_service.h"
#include "dfs/managers/headers/dfs_networkmanager.h"

QTcpSocket *TcpSocketService::socket() const
{
    return m_tcp;
}

void TcpSocketService::setSocket(QTcpSocket *value)
{
    m_tcp = value;
}

bool TcpSocketService::isActive() const
{
    return active;
}

void TcpSocketService::setNetworkManager(NetworkManager *value)
{
    networkManager = value;
}

TcpSocketService::TcpSocketService()
{
    // dpBuffer->clear();
}

TcpSocketService::TcpSocketService(const TcpSocketService &value)
{
    socketDescriptor = value.socketDescriptor;
    active = value.active;
    address = value.address;
    m_port = value.m_port;
    m_tcp = value.m_tcp;
    m_identifier = value.m_identifier;
    _blockSize = value._blockSize;
    // buffer = value.buffer;
    // dpBuffer->clear();
}

TcpSocketService::TcpSocketService(QString address, quint16 networkPort, QObject *parent)
//    : QObject(parent)
{
    this->address = address;
    this->m_port = networkPort;
    // dpBuffer->clear();
}

TcpSocketService::TcpSocketService(qintptr socketDescriptor, QObject *parent)
//    : QObject(parent)
{
    this->socketDescriptor = socketDescriptor;
    // dpBuffer->clear();
    qDebug() << "[TCP] Socket Descriptor" << socketDescriptor;
}

TcpSocketService::~TcpSocketService()
{
    if (m_tcp == nullptr)
        return;
    m_tcp->close();
    m_tcp->deleteLater();
    qDebug() << "[TCP] Remove tcp socket" << address << port() << serverPort();
}

void TcpSocketService::sendMessage(const QByteArray &data)
{
    if (!m_tcp->isValid())
        return;

    QByteArray _wtSok = Serialization::serialize({ data }, Messages::FIELD_SIZE); // qCompress
    m_tcp->write(_wtSok, _wtSok.size());
}

void TcpSocketService::process()
{
    if (m_tcp == nullptr)
    {
        this->m_tcp = new QTcpSocket(this);
        connect(this, &TcpSocketService::send, this, &TcpSocketService::sendMessage, Qt::QueuedConnection);
        connect(m_tcp, &QTcpSocket::readyRead, this, &TcpSocketService::doRead, Qt::QueuedConnection);
        connect(m_tcp, &QTcpSocket::connected, this, &TcpSocketService::establishConnection);
        connect(m_tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError socketError) {
            Q_UNUSED(socketError)
            qDebug().noquote() << "[TCP] Socket error" << socketError << "for" << address << port()
                               << serverPort();
            if (this->m_tcp->state() != QTcpSocket::ConnectedState)
                emit this->close();
        });
    }

    if (socketDescriptor != 0)
    {
        this->m_tcp->setSocketDescriptor(socketDescriptor);
        establishConnection();
    }
    else
    {
        this->m_tcp->connectToHost(address, m_port);
    }
}

void TcpSocketService::establishConnection()
{
    qDebug() << "[TCP] Thread:" << this->thread() << "| Valid:" << m_tcp->isValid();
    this->address = QHostAddress(this->m_tcp->peerAddress().toIPv4Address()).toString();
    QByteArray idb = m_IdentifierPrefix
        + Serialization::serialize(
                         { QByteArray::number(0), Network::currentIdentifier(),
                           "" }); // TODO: remove build & networkManager->getSerializedConnectionList()

    emit send(idb);

    qDebug() << "[TCP] Address" << this->m_tcp << address << port() << serverPort() << m_tcp->localPort()
             << m_tcp->peerPort();
    qDebug() << "[TCP] Open status:" << m_tcp->isOpen();
}

void TcpSocketService::setActive(bool active)
{
    this->active = active;
}

void TcpSocketService::doRead()
{
    if (pendMsgSize >= 0)
    {
        continueDoRead();
    }
    if (m_tcp->bytesAvailable() >= Config::Net::PROTOCOL_VERSION.size() + 8)
    {
        QByteArray data = m_tcp->read(Messages::FIELD_SIZE);
        pendMsgSize = Utils::qByteArrayToInt(data);
        if ((pendMsgSize > 0))
        {
            continueDoRead();
        }
    }
}

void TcpSocketService::continueDoRead()
{
    if (m_tcp->bytesAvailable() >= pendMsgSize)
    {
        char *pckg = new char[pendMsgSize];

        int bytesRead = m_tcp->read(pckg, pendMsgSize);
        QByteArray rpckg(pckg, bytesRead);
        // rpckg.append(pckg);

        if (rpckg.size() > bytesRead)
        {
            rpckg.remove(bytesRead - 1, rpckg.size() - bytesRead);
        }
        if (pendMsgSize == bytesRead)
        {
            pendMsg.append(rpckg);
            if (!this->isActive() && pendMsg.left(m_IdentifierPrefix.size()) == m_IdentifierPrefix)
            {
                QByteArray b = pendMsg.mid(m_IdentifierPrefix.size());

                QByteArrayList bl = Serialization::deserialize(b);
                if (bl.length() == 3)
                {

                    m_identifier = bl[1];
                    // networkManager->connectToServerByIpList(Serialization::deserialize(bl[2]));
                }
                else
                {
                    emit this->close();
                }
            }
            else
            {
                SocketPair receiver(this->ip().toStdString(), this->port());
                receiver.setIdentifier(this->identifier().toLatin1());
                this->gotMessage(pendMsg, receiver);
            }
            pendMsgSize = -1;
            pendMsg = "";
        }
        else
        {
            pendMsgSize = pendMsgSize - bytesRead;
            pendMsg.append(rpckg);
        }
        delete[] pckg;
        if (m_tcp->bytesAvailable() >= pendMsgSize)
        {
            doRead();
        }
    }
}

void TcpSocketService::gotMessage(QByteArray msg, SocketPair rec)
{
    QByteArray bmsg = msg;
    Messages::BaseMessage dbm;
    dbm.deserialize(bmsg);
    if (dbm.isEmpty())
        return;
    if (dbm.protocol != Config::Net::PROTOCOL_VERSION)
        return;

    if (bmsg == Config::Net::PROTOCOL_VERSION)
    {
        qDebug() << "[TCP] Protocol msg collected";
    }

    if (serverPort() == 2223)
        reinterpret_cast<DfsNetworkManager *>(networkManager)->messageReceived(bmsg, rec);
    else
        networkManager->messageReceived(bmsg, rec);
}

const QString &TcpSocketService::identifier() const
{
    return m_identifier;
}

QString TcpSocketService::ip() const
{
    return address;
}

quint16 TcpSocketService::port() const
{
    if (m_tcp->peerPort() != networkManager->tcpPort)
        return m_tcp->peerPort();
    else
        return m_tcp->localPort();
}

quint16 TcpSocketService::serverPort() const
{
    if (m_tcp->peerPort() == networkManager->tcpPort)
        return m_tcp->peerPort();
    else
        return m_tcp->localPort();
}

QString TcpSocketService::protocolString() const
{
    return "TCP";
}

Network::Protocol TcpSocketService::protocol() const
{
    return Network::Protocol::Tcp;
}

QHostAddress TcpSocketService::getSocketAddress() const
{
    return m_tcp->peerAddress();
}
