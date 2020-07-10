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

#ifndef DISCOVERY_SERVICE_H
#define DISCOVERY_SERVICE_H

#include <QObject>
#include <QtNetwork/QUdpSocket>
#include <QtNetwork/QNetworkDatagram>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <chrono>
#include <thread>

#include "utils/utils.h"
#include "network/packages/service/ping_pong_message.h"

class DiscoveryService : public QObject
{
    Q_OBJECT
private:
    quint16 netPort; // network port
    quint16 port;    // discovery port
    QNetworkAddressEntry *local;
    QUdpSocket *socket;
    bool active = false;

public:
    DiscoveryService(quint16 discoveryPort, quint16 networkPort, QNetworkAddressEntry *local);
    ~DiscoveryService();

public slots:
    /**
     * @brief run IPv4 scan
     */
    void process();
private slots:
    /**
     * @brief Process recieved messages and answer them
     */
    void recieveMsg();

    // public:
    //    /**
    //     * @brief enable
    //     */
    //    void enable();
    //    /**
    //     * @brief disable
    //     */
    //    void disable();
    //    /**
    //     * @brief isActive status
    //     * @return status
    //     */
    //    bool isActive() const;
signals:
    /**
     * @brief ClientDiscovered signal for socket and server service
     * @param address
     */
    void ClientDiscovered(QHostAddress address, quint16 port);
    /**
     * @brief finished thread
     */
    void finished();
};
#endif // DISCOVERY_SERVICE_H
