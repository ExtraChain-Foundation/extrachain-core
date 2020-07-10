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

#include "network/server_service.h"

ServerService::ServerService(quint16 networkPort, QNetworkAddressEntry *local, QTcpServer *parent)
    : QTcpServer(parent)
    , localAddress(local)
    , port(networkPort)
{
}

void ServerService::startListen()
{
    bool status = this->listen(localAddress->ip(), port);
    qDebug() << "Server listening status:" << status;
    emit serverStatus(status);

    if (!status)
    {
#ifdef ECONSOLE
        if (serverError() == QAbstractSocket::AddressInUseError)
        {
            qInfo().nospace().noquote() << "---> [Error] Address " << localAddress->ip().toString() << ":"
                                        << port << " already in use";
            std::exit(0);
        }
        else
#endif
            qDebug() << "Server error:" << serverError();
        qDebug() << "emit startError";
    }
    else
    {
        qDebug() << "Server address:" << this->serverAddress() << "| server port:" << this->serverPort();
    }
}

ServerService::~ServerService()
{
    //    active = false;
    emit finished();
}

int ServerService::process()
{
    //    qDebug() << " slot PROCCES server->hasPendingConnections()"
    //             << server->hasPendingConnections();
    //    if (server->hasPendingConnections())
    //    {
    //        //        QTcpSocket *newSocket = server->nextPendingConnection();
    //        //        if (newSocket->peerAddress() != QHostAddress(""))
    //        //        {
    //        //            emit newConnection(newSocket);
    //        //            qDebug() << "SERVER SERVICE: new connection, socket address:"
    //        //                     << newSocket->peerAddress() << ":" <<
    //        newSocket->peerPort();
    //        //        }
    //        //        else
    //        //        {
    //        //            qDebug() << "SERVER SERVICE: new forbidden connection, socket
    //        address:
    //        "
    //        //                     << newSocket->peerAddress() << ":" <<
    //        newSocket->peerPort();
    //        //        }
    //    }
    return 0;
}

void ServerService::incomingConnection(qintptr socketDescriptor)
{
    emit newConnection(socketDescriptor);
}

// void ServerService::socketMsg()
//{
//    QTcpSocket *sock = qobject_cast<QTcpSocket *>(QObject::sender());
//    QHostAddress peerAddress = QHostAddress(sock->peerAddress().toIPv4Address());
//    QByteArray msgs = sock->readAll();
//    qDebug() << "SERVER SERVICE: socketMsg: from - " << sock;
//    // qDebug() << "SERVER SERVICE: Received messages:" << msgs << " from " +
//    //             peerAddress.toString();
//    if (msgs.isEmpty())
//    {
//        qDebug() << "[Warning] Received empty message from" + peerAddress.toString();
//        return;
//    }

//    for (QByteArray msg :
//         Serialization::deserialize(msgs, Serialization::INFORMATION_SEPARATOR_TWO))
//    {
//        if (msg.isEmpty())
//        {
//            qDebug() << ";adfkslj";
//            continue;
//        }
//        // qDebug() << "SERVER SERVICE: Received message:" << msg;
//        emit MessageReceived(msg, peerAddress);
//    }
//}

void ServerService::socketDisconnected()
{
    //
}
