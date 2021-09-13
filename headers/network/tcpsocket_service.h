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
class EXTRACHAIH_EXPORT TcpSocketService : public SocketService
{
    Q_OBJECT

public:
    explicit TcpSocketService();
    explicit TcpSocketService(QString address, NetworkManager *networkManager, QObject *parent = nullptr);
    explicit TcpSocketService(qintptr socketDescriptor, NetworkManager *networkManager,
                              QObject *parent = nullptr);
    TcpSocketService(const TcpSocketService &value);
    ~TcpSocketService();

public slots:
    /**
     * @brief Send message using QTcpSocket
     * @param message
     */
    void sendMessage(const QByteArray &data);
    void process();
    void establishConnection();
    void closeSocket() override;

private slots:
    void doRead();

public:
    bool isActive() const override;
    quint16 port() const override;
    quint16 serverPort() const override;
    QString protocolString() const override;
    Network::Protocol protocol() const override;
    QTcpSocket *socket() const;

private:
    qintptr socketDescriptor = 0;
    QTcpSocket *m_tcp = nullptr;
    int _blockSize = 0;
    QByteArray pendMsg;
    int pendMsgSize = -1;

    void gotMessage(const QByteArray &msg, const SocketPair &pair);
    void continueDoRead();
};
#endif // SOCKET_SERVICE_H
