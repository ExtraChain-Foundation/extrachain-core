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

TcpSocketService::TcpSocketService()
    : SocketService(nullptr, nullptr)
{
    qFatal("tcp test");
}

TcpSocketService::TcpSocketService(QString address, NetworkManager *networkManager, QObject *parent)
    : SocketService(networkManager, parent)
{
    this->address = address;
    // dpBuffer->clear();
}

TcpSocketService::TcpSocketService(qintptr socketDescriptor, NetworkManager *networkManager, QObject *parent)
    : SocketService(networkManager, parent)
{
    this->socketDescriptor = socketDescriptor;
    // dpBuffer->clear();
    qDebug() << "[TCP] Socket Descriptor" << socketDescriptor;
}

TcpSocketService::TcpSocketService(const TcpSocketService &value)
    : SocketService(value)
{
    m_tcp = value.m_tcp;
    socketDescriptor = value.socketDescriptor;
    address = value.address;
    _blockSize = value._blockSize;
    // buffer = value.buffer;
    // dpBuffer->clear();
}

TcpSocketService::~TcpSocketService()
{
    if (m_tcp == nullptr)
        return;
    m_tcp->close();
    m_tcp->deleteLater();
    qDebug() << "[TCP] Remove tcp socket" << address << port() << serverPort();
}

QTcpSocket *TcpSocketService::socket() const
{
    return m_tcp;
}

bool TcpSocketService::isActive() const
{
    return m_tcp->isValid() && m_activated;
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
    else
    {
        qFatal("[TCP] tcp != nullptr in process");
    }

    if (socketDescriptor != 0)
    {
        this->m_tcp->setSocketDescriptor(socketDescriptor);
        establishConnection();
    }
    else
    {
        qDebug() << "[TCP]" << this->m_tcp;
        this->m_tcp->connectToHost(address, m_networkManager->tcpPort);
    }
}

void TcpSocketService::establishConnection()
{
    qDebug() << "[TCP] Thread:" << this->thread() << "| Valid:" << m_tcp->isValid();
    this->address = QHostAddress(this->m_tcp->peerAddress().toIPv4Address()).toString();

    emit send(generateFirstMessage());

    qDebug() << "[TCP] Address" << this->m_tcp << address << port() << serverPort() << m_tcp->localPort()
             << m_tcp->peerPort();
    qDebug() << "[TCP] Open status:" << m_tcp->isOpen();
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
            if (!m_activated && pendMsg.left(2) == "{\"")
            {
                m_activated = checkFirstMessage(pendMsg);
                qDebug() << "[TCP] First message" << pendMsg << m_activated;
            }
            else
            {
                SocketPair receiver(address.toStdString(), this->port());
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
        qFatal("dmb is empty");
    if (dbm.protocol != Config::Net::PROTOCOL_VERSION)
        qFatal("protocol");

    if (bmsg == Config::Net::PROTOCOL_VERSION)
    {
        qDebug() << "[TCP] Protocol msg collected";
    }
}

quint16 TcpSocketService::port() const
{
    if (m_tcp->peerPort() != m_networkManager->tcpPort)
        return m_tcp->peerPort();
    else
        return m_tcp->localPort();
}

quint16 TcpSocketService::serverPort() const
{
    if (m_tcp->peerPort() == m_networkManager->tcpPort)
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
