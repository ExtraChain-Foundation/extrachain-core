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

#include "network/discovery_service.h"

DiscoveryService::DiscoveryService(quint16 discoveryPort, quint16 networkPort, QNetworkAddressEntry *local)
    : local(local)
{
    qDebug() << "DISCOVERY SERVICE: constructor";
    netPort = networkPort;
    port = discoveryPort;
    socket = new QUdpSocket();
    socket->bind(QHostAddress::Any, port);
    connect(socket, &QUdpSocket::readyRead, this, &DiscoveryService::recieveMsg);
}

DiscoveryService::~DiscoveryService()
{
    emit finished();
    active = false;
    //    this->disable();
    delete socket;
}

void DiscoveryService::process()
{
    qDebug() << "DISCOVERY SERVICE: process start";
    active = true;
    foreach (QNetworkInterface interface, QNetworkInterface::allInterfaces())
    {
        if (interface.type() != QNetworkInterface::Wifi)
        {
            continue;
        }
        qDebug() << interface.flags() << " " << interface.name() << " " << interface.type();
        foreach (QNetworkAddressEntry entry, interface.addressEntries())
        {
            QHostAddress broadcastAddress = entry.broadcast();
            if (broadcastAddress != QHostAddress::Null
                && entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                && broadcastAddress != QHostAddress::LocalHost
                && broadcastAddress != QHostAddress(QHostAddress::LocalHost))
            {
                qDebug() << broadcastAddress << " " << QHostAddress(QHostAddress::LocalHost);
                if (broadcastAddress != local->ip())
                    socket->writeDatagram(Messages::createPingMessage(), broadcastAddress, port);
            }
        }
    }
    while (active)
    {
        QRandomGenerator randHost;
        for (quint32 i = randHost.bounded(quint32(1), QHostAddress("255.255.255.255").toIPv4Address());
             i <= QHostAddress("255.255.255.255").toIPv4Address(); i++)
        {
            // qDebug() << "DISCOVERY SERVICE: finder";
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            socket->writeDatagram(Messages::createPingMessage(), QHostAddress(i /*"51.68.181.53"*/), port);
            // qDebug() << "udp send message";
        }
    }
}

void DiscoveryService::recieveMsg()
{
    qDebug() << "DISCOVERY SERVICE: recieveMsg";
    QNetworkDatagram datagram = socket->receiveDatagram();
    if (Messages::isPing(datagram.data()))
    {
        qDebug() << "Ping message is received from"
                 << QHostAddress(datagram.senderAddress().toIPv4Address()).toString();
        socket->writeDatagram(Messages::createPongMessage(netPort),
                              QHostAddress(datagram.senderAddress().toIPv4Address()), port);
        //        emit ClientDiscovered(QHostAddress(datagram.senderAddress().toIPv4Address()).toString(),
        //                              port);
        return;
    }
    if (Messages::isPong(datagram.data()))
    {
        qDebug() << "Pong message is received from"
                 << QHostAddress(datagram.senderAddress().toIPv4Address()).toString();
        QString sender = QHostAddress(datagram.senderAddress().toIPv4Address()).toString();
        QJsonDocument doc = QJsonDocument::fromJson(datagram.data());
        int prt = doc.object().value("netPort").toString().toInt();
        qDebug() << "DISCOVERY SERVICE: port" << prt;
        emit ClientDiscovered(datagram.senderAddress(), static_cast<quint16>(prt));
        return;
    }
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
