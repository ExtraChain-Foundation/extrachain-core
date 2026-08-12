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
#ifndef UPNPCONNECTOR_H
#define UPNPCONNECTOR_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkAccessManager>
#include <QNetworkAddressEntry>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <QHostAddress>

#include <memory>

class UPnPConnector : public QObject {
    Q_OBJECT
public:
    explicit UPnPConnector(std::shared_ptr<QNetworkAddressEntry> local, QObject *parent = nullptr);
    ~UPnPConnector();

    // Starts an SSDP discovery to check for UPnP devices.
    void discoverDevices();

    // After a device is discovered and its control URL is known,
    // you can call these methods to query and establish tunnels.
    void getExternalIPAddress(const QUrl &controlUrl);
    void addPortMapping(const QUrl    &controlUrl,
                        int            internalPort,
                        int            externalPort,
                        const QString &protocol,
                        const QString &description,
                        const QString &internalClient);
    void removePortMapping(const QUrl &controlUrl, int externalPort, const QString &protocol);
    void retrieveDeviceDescription(const QUrl &deviceDescriptionUrl);

    void getSpecificPortMappingEntry(const QUrl &controlUrl, int externalPort, const QString &protocol);

signals:
    // Emitted when a UPnP device is discovered.
    void deviceDiscovered(const QHostAddress &address, const QString &location);

    // Emitted when a SOAP response is received.
    void soapResponseReceived(const QString &response);

    // Emitted when a WANIPConnection control URL found.
    void controlURLFound(const QString &controlURL);

    // Emitted if an error occurs.
    void errorOccurred(const QString &errorMessage);

    // Emitted when the external IP is obtained.
    void externalIPAddressObtained(const QString &externalIP);

    // Emitted when a port mapping is successfully added.
    void portMappingAdded();

    // Emitted when a port mapping is successfully removed.
    void portMappingRemoved();

private slots:
    // Handle incoming UDP responses.
    void onUdpReadyRead();

    // Handle finished HTTP requests.
    void onHttpFinished(QNetworkReply *reply);

    // Handle discovery timeout.
    void onTimeout();

private:
    // Utility to send SOAP requests.
    void postSOAP(const QUrl &controlUrl, const QString &soapAction, const QString &message);

    QUdpSocket                           *udpSocket;
    QNetworkAccessManager                *networkManager;
    QTimer                               *timeoutTimer;
    std::shared_ptr<QNetworkAddressEntry> localAddress;

    // Save the current SOAP action if needed to process responses.
    QString currentSoapAction;
};

#endif // UPNPCONNECTOR_H
