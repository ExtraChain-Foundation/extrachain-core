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

#ifndef SERVER_SERVICE_H
#define SERVER_SERVICE_H

#include <QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QNetworkAddressEntry>
#include <QThread>
#include <iostream>
//#include "resolver_service.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "utils/utils.h"

/**
 * @brief ServerService is responsible for handling incoming sockets connections.
 * All connections is stored in a sockets list.
 */
class ServerService : public QTcpServer
{
    Q_OBJECT
private:
    // bool active = false;
    QNetworkAddressEntry *localAddress;
    quint16 port;

public:
    ServerService(quint16 networkPort, QNetworkAddressEntry *local, QTcpServer *parent = nullptr);
    ~ServerService() override;

public slots:
    // void run() override;
    int process();

public:
    void startListen();
    bool isActive() const;

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    /**
     * @brief Initial processing of incoming messages.
     * Reads message from socket and emits MessageReceived signal.
     */
    //    void socketMsg();
    void socketDisconnected();

signals:
    void MessageReceived(QByteArray msg, QHostAddress peerAddress);
    void finished();
    // void newSocketAdd(QTcpSocket *addSocket);
    void newConnection(qint64 socketDescriptor);
    void serverStatus(bool socketError);
};
#endif // SERVER_SERVICE_H
