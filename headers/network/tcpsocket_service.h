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

#include "network/socket_pair.h"
#include "utils/exc_utils.h"

class NetworkManager;

/**
 * @brief SocketService is responsible for message delivery
 */
class TcpSocketService : public QObject
{
    Q_OBJECT

private:
    NetworkManager *networkManager = nullptr;
    qintptr socketDescriptor = 0;
    bool active = false;
    QString address;
    quint16 m_port;
    QTcpSocket *m_tcp = nullptr;
    QString m_identifier;
    int _blockSize = 0;
    QByteArray pendMsg;
    int pendMsgSize = -1;
    const QByteArray m_IdentifierPrefix = "ind:";

    int m_bytesIncoming = 0;
    int m_bytesOutgoing = 0;
    int m_bytesCompressed = 0;

public:
    TcpSocketService();
    TcpSocketService(const TcpSocketService &value);
    TcpSocketService(QString address, quint16 networkPort, QObject *parent = nullptr);
    TcpSocketService(qintptr socketDescriptor, QObject *parent = nullptr);
    ~TcpSocketService() override;

signals:
    void send(const QByteArray &data);
    // void MessageReceived(const QByteArray &msgS, const SocketPair &receiver);
    void close();
    void finished();
    void setActiveSignal(bool active);

public slots:
    /**
     * @brief Send message using QTcpSocket
     * @param message
     */
    void sendMessage(const QByteArray &data);
    void process();
    void establishConnection();
    void setActive(bool active);

private slots:
    void doRead();
    void continueDoRead();

public:
    void gotMessage(QByteArray msg, SocketPair rec);
    const QString &identifier() const;

    bool isActive() const;
    QString ip() const;
    quint16 port() const;
    quint16 serverPort() const;
    QString protocolString() const;
    Network::Protocol protocol() const;

    QHostAddress getSocketAddress() const;
    QTcpSocket *socket() const;
    void setSocket(QTcpSocket *value);
    void setNetworkManager(NetworkManager *value);

    int bytesIncoming() const
    {
        return m_bytesIncoming;
    }
    int bytesOutgoing() const
    {
        return m_bytesOutgoing;
    }
    int bytesCompressed() const
    {
        return m_bytesCompressed;
    }
};
#endif // SOCKET_SERVICE_H
