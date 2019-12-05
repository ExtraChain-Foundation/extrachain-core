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
#ifdef ETALONIUM_CONSOLE
        if (serverError() == QAbstractSocket::AddressInUseError)
        {
            qInfo().nospace() << "Address " << localAddress->ip().toString() << ":" << port
                              << " already in use";
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
    //        //            qDebug() << "SERVER SERVICE: new connection, socket address: "
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
//    // qDebug() << "SERVER SERVICE: Received messages: " << msgs << " from " +
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
//        // qDebug() << "SERVER SERVICE: Received message: " << msg;
//        emit MessageReceived(msg, peerAddress);
//    }
//}

void ServerService::socketDisconnected()
{
    //
}
