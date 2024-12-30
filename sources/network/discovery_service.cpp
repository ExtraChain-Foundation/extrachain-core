/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include "network/discovery_service.h"

#include "utils/exc_logs.h"

DiscoveryService::DiscoveryService(quint16 discoveryPort, quint16 networkPort, QNetworkAddressEntry *local)
    : local(local) {
    eLog("[DiscoveryService] constructor");
    netPort = networkPort;
    port    = discoveryPort;
    socket  = new QUdpSocket();
    socket->bind(QHostAddress::Any, port);
    connect(socket, &QUdpSocket::readyRead, this, &DiscoveryService::recieveMsg, Qt::QueuedConnection);
}

DiscoveryService::~DiscoveryService() {
    emit finished();
    active = false;
    //    this->disable();
    delete socket;
}

void DiscoveryService::process() {
    eLog("[DiscoveryService] process start");
    active = true;
    foreach (QNetworkInterface networkInterface, QNetworkInterface::allInterfaces()) {
        if (networkInterface.type() != QNetworkInterface::Wifi) {
            continue;
        }

        eLog("{}", networkInterface);

        foreach (QNetworkAddressEntry entry, networkInterface.addressEntries()) {
            QHostAddress broadcastAddress = entry.broadcast();
            if (broadcastAddress != QHostAddress::Null && entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                && broadcastAddress != QHostAddress::LocalHost
                && broadcastAddress != QHostAddress(QHostAddress::LocalHost)) {
                eLog("{} {}", broadcastAddress, QHostAddress(QHostAddress::LocalHost));
                // if (broadcastAddress != local->ip())
                //    socket->writeDatagram(Messages::createPingMessage(), broadcastAddress, port);
            }
        }
    }

    while (active) {
        QRandomGenerator randHost;
        for (quint32 i = randHost.bounded(quint32(1), QHostAddress("255.255.255.255").toIPv4Address());
             i <= QHostAddress("255.255.255.255").toIPv4Address();
             i++) {
            // eLog("[DiscoveryService] Finder");
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // socket->writeDatagram(Messages::createPingMessage(), QHostAddress(i /*"ip"*/), port);
            // eLog("[DiscoveryService] udp send message");
        }
    }
}

void DiscoveryService::recieveMsg() {
    eLog("[DiscoveryService] recieveMsg");
    QNetworkDatagram datagram = socket->receiveDatagram();
    /*
    if (Messages::isPing(datagram.data())) {
        eLog("Ping message is received from {}",
    QHostAddress(datagram.senderAddress().toIPv4Address()).toString());
        socket->writeDatagram(Messages::createPongMessage(netPort),
                              QHostAddress(datagram.senderAddress().toIPv4Address()), port);
        //        emit ClientDiscovered(QHostAddress(datagram.senderAddress().toIPv4Address()).toString(),
        //                              port);
        return;
    }
    if (Messages::isPong(datagram.data())) {
        eLog("Pong message is received from {}",
    QHostAddress(datagram.senderAddress().toIPv4Address()).toString());
        // QString sender = QHostAddress(datagram.senderAddress().toIPv4Address()).toString();
        QJsonDocument doc = QJsonDocument::fromJson(datagram.data());
        int prt = doc.object().value("netPort").toString().toInt();
        eLog("[DiscoveryService] port {}", prt);
        emit ClientDiscovered(datagram.senderAddress(), static_cast<quint16>(prt));
        return;
    }
    */
}

// void DiscoveryService::enable()
//{
//    active = true;
//    //    this->start();
//}

// void DiscoveryService::disable()
//{
//    active = false;
//}

// bool DiscoveryService::isActive() const
//{
//    return active;
//}
