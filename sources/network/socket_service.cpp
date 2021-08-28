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

#include "network/socket_service.h"
#include "dfs/managers/headers/dfsnetmanager.h"

QTcpSocket *SocketService::socket() const
{
    return m_tcp;
}

void SocketService::setSocket(QTcpSocket *value)
{
    m_tcp = value;
}

QAbstractSocket::SocketState SocketService::state()
{
    return m_tcp->state();
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

SocketPair SocketService::getSocketPair()
{
    SocketPair res(address.toStdString(), m_port);
    res.iden = identificator.toByteArray();
    return res;
}

void SocketService::setNetManager(NetManager *value)
{
    netManager = value;
}

SocketService::SocketService()
{
    this->identificator = BigNumber("0");
    //    dpBuffer->clear();
}

SocketService::SocketService(const SocketService &value)
{
    connectionTry = value.connectionTry;
    socketDescriptor = value.socketDescriptor;
    active = value.active;
    address = value.address;
    m_port = value.m_port;
    m_tcp = value.m_tcp;
    identificator = value.identificator;
    _blockSize = value._blockSize;
    // buffer = value.buffer;
    reconnectTry = value.reconnectTry;
    // dpBuffer->clear();
}

SocketService::SocketService(QString address, quint16 networkPort, QObject *parent)
//    : QObject(parent)
{
    this->address = address;
    this->m_port = networkPort;
    //    dpBuffer->clear();
}

SocketService::SocketService(qintptr socketDescriptor, QObject *parent)
//    : QObject(parent)
{
    this->socketDescriptor = socketDescriptor;
    //    dpBuffer->clear();
    qDebug() << "[Socket Service] Socket Descriptor" << socketDescriptor;
}

SocketService::~SocketService()
{
    if (m_tcp == nullptr)
        return;
    m_tcp->close();
    m_tcp->deleteLater();
    qDebug() << "[Socket Service] Remove SocketService" << address << m_port;
}

void SocketService::sendMsg(const QByteArray &data, const SocketPair &socketData)
{
    Q_UNUSED(socketData)
    // if(all)
    // send
    // if(allexcept && adress != closedAdress)
    // send
    // if(focused && adress == mustAdress)
    // send

    // check socket status
    if (!m_tcp->isValid())
        return;
    // take data from pair
    // QString ipAddress = QString::fromStdString(socketData.ip);
    // qint64 portAddress = socketData.port;
    // take socket which we need if we have 0 - port and 0.0.0.0 - ip address send anyway
    //    if (((ipAddress == address) || ipAddress == "0.0.0.0") && ((port == portAddress) || (portAddress ==
    //    0)))
    //    {

    QByteArray _wtSok = Serialization::serialize({ data }, Messages::FIELD_SIZE);
    m_tcp->write(_wtSok, _wtSok.size());
    //    }
}

void *SocketService::distMsg(const QByteArray data, const SocketPair &socketData)
{
    //    QThread::currentThread()->msleep(100);
    emit msgReady(data, socketData);
    // QCoreApplication::processEvents();
    return nullptr;
}

void SocketService::process()
{
    if (m_tcp == nullptr)
    {
        this->m_tcp = new QTcpSocket(this);
        connect(this, &SocketService::msgReady, this, &SocketService::sendMsg, Qt::QueuedConnection);
        connect(m_tcp, &QTcpSocket::connected, this, &SocketService::connected);
        connect(m_tcp, &QTcpSocket::disconnected, this, &SocketService::reconnect);
        connect(m_tcp, &QTcpSocket::readyRead, this, &SocketService::doRead, Qt::QueuedConnection);
        connect(m_tcp, &QTcpSocket::connected, this, &SocketService::establishConnection);
        connect(m_tcp, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError socketError) {
            Q_UNUSED(socketError)
            qDebug().nospace().noquote()
                << "[Socket Service] Socket error " << socketError << " for " << address << ":" << m_port;
            if (this->m_tcp->state() != QTcpSocket::ConnectedState)
                this->reconnect();
        });
        connect(this, &SocketService::setActiveSignal, this, &SocketService::setActive);
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
    //    QCoreApplication::processEvents();
}

void SocketService::establishConnection()
{
    qDebug() << "[Socket Service] Thread:" << this->thread() << "| Valid:" << m_tcp->isValid();
    this->address = QHostAddress(this->m_tcp->peerAddress().toIPv4Address()).toString();
    this->m_port = this->m_tcp->peerPort();
    QByteArray idb = IDENTIFICATOR
        + Serialization::serialize({ QByteArray::number(0), net::readNetManagerIdentificator(),
                                     netManager->getSerializedConnectionList() }); // TODO: remove build
    this->distMsg(idb, SocketPair(this->address.toStdString(), this->m_port));

    qDebug() << "[Socket Service] Address" << this->m_tcp << address << m_port;
    qDebug() << "[Socket Service] Open status:" << m_tcp->isOpen();
}

void SocketService::setActive(bool active)
{
    this->active = active;
}

void SocketService::doRead()
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

void SocketService::continueDoRead()
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
            if (!this->isActive() && pendMsg.left(IDENTIFICATOR.size()) == IDENTIFICATOR)
            {
                QByteArray b = pendMsg.mid(IDENTIFICATOR.size());

                QByteArrayList bl = Serialization::deserialize(b);
                if (bl.length() == 3)
                {

                    this->processID(bl[1]);
                    netManager->addTempConnections(Serialization::deserialize(bl[2]));
                    netManager->checkOnValidConnection(this->getID().toByteArray(), this->ip().toLocal8Bit());
                    netManager->connectToServerByIpList(Serialization::deserialize(bl[2]));
                }
                else
                {
                    emit this->removeMe();
                }
            }
            else
            {
                SocketPair receiver(this->ip().toStdString(), this->port());
                receiver.setId(this->getID().toByteArray());
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

void SocketService::gotMessage(QByteArray msg, SocketPair rec)
{
    // msg->get protocol -> end socket
    // netManager list connections
    QByteArray bmsg = msg;
    Messages::BaseMessage dbm;
    dbm.deserialize(bmsg);
    if (dbm.isEmpty())
        return;
    if (dbm.protocol != Config::Net::PROTOCOL_VERSION)
        return;
    //    QByteArrayList msgList = Serialization::deserialize(bmsg, 8);
    //    QByteArray checkProtocol;
    //    if (msgList.length() > 0)
    //        checkProtocol = msgList.at(0);
    //    if (checkProtocol != Config::Net::PROTOCOL_VERSION)
    //    {
    //        qDebug().noquote().nospace() << "Incorrect protocol version for " << address << ":" << port;
    //        this->removeMe();
    //    }
    if (bmsg == Config::Net::PROTOCOL_VERSION)
    {
        qDebug() << "[Socket Service] Protocol msg COLLECTED";
        counter = 1;
    }
    //    switch (counter)
    //    {
    //    case 0:
    //        break;
    //    case 1:
    //        bm.protocol = bmsg;
    //        //        bmr.protocol = msgList.at(0);
    //        counter++;
    //        return;
    //        //        break;
    //    case 2:
    //        bm.type = bmsg.toUInt();
    //        //        bmr.type = msgList.at(0).toUInt();
    //        counter++;
    //        return;
    //    case 3:
    //        bm.signer = BigNumber(bmsg);
    //        //        bmr.signer = BigNumber(msgList.at(0));
    //        counter++;
    //        return;
    //    case 4:
    //        bm.digSig = bmsg;
    //        //        bmr.digSig = msgList.at(0);
    //        counter++;
    //        return;
    //    case 5:
    //        bm.data = bmsg;
    //        //        bmr.data = msgList.at(0);
    //        counter = 0;
    //        bmsg = bm.serialize();
    //        break;
    //        //        if (Messages::isGeneralResponse(bm.type))
    //        //        {
    //        //            counter++;
    //        //            return;
    //        //        }
    //        //    case 6:
    //        //        bm.type = msgList.at(0).toUInt();
    //        //        bmr.type = msgList.at(0).toUInt();
    //        //        counter = 0;
    //        //        break;
    //    }
    if (m_tcp->localPort() == 2223 || m_tcp->localPort() == 2224)
    {
        reinterpret_cast<DFSNetManager *>(netManager)->MessageReceived(bmsg, rec);
    }
    else
        netManager->MessageReceived(bmsg, rec);
}

const BigNumber &SocketService::getID()
{
    return identificator;
}

void SocketService::processID(QByteArray id)
{
    identificator = BigNumber(id);
    emit checkMe();
}

bool SocketService::isActive() const
{
    return active;
}

QString SocketService::ip() const
{
    return address;
}

quint16 SocketService::port() const
{
    return m_port;
}

QString SocketService::protocolString() const
{
    return "TCP";
}

Network::Protocol SocketService::protocol() const
{
    return Network::Protocol::Tcp;
}

QHostAddress SocketService::getSocketAddress() const
{
    return m_tcp->peerAddress();
}

quint16 SocketService::getSocketPeer() const
{
    return m_tcp->peerPort();
}
