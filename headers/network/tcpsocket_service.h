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

#ifndef SOCKET_SERVICE_H
#define SOCKET_SERVICE_H

#include <QTcpSocket>

#include "network/isocket_service.h"
#include "network/socket_pair.h"
#include "utils/exc_utils.h"

class NetworkManager;

/**
 * @brief SocketService is responsible for message delivery
 */
class TcpSocketService : public SocketService
{
    Q_OBJECT

public:
    explicit TcpSocketService();
    explicit TcpSocketService(QString address, NetworkManager *networkManager, QObject *parent = nullptr);
    explicit TcpSocketService(qintptr socketDescriptor, NetworkManager *networkManager,
                              QObject *parent = nullptr);
    TcpSocketService(const TcpSocketService &value);
    ~TcpSocketService();

signals:
    void close();
    void finished();

public slots:
    /**
     * @brief Send message using QTcpSocket
     * @param message
     */
    void sendMessage(const QByteArray &data);
    void process();
    void establishConnection();

private slots:
    void doRead();

public:
    bool isActive() const;
    quint16 port() const;
    quint16 serverPort() const;
    QString protocolString() const override;
    Network::Protocol protocol() const override;
    QTcpSocket *socket() const;

private:
    qintptr socketDescriptor = 0;
    QString address;
    QTcpSocket *m_tcp = nullptr;
    int _blockSize = 0;
    QByteArray pendMsg;
    int pendMsgSize = -1;

    void gotMessage(QByteArray msg, SocketPair rec);
    void continueDoRead();
};
#endif // SOCKET_SERVICE_H
