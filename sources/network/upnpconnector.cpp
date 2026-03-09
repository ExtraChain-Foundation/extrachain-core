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
#include "network/upnpconnector.h"
#include <QDebug>
#include <QNetworkRequest>
#include <iostream>

UPnPConnector::UPnPConnector(std::shared_ptr<QNetworkAddressEntry> local, QObject *parent)
    : QObject(parent)
    , udpSocket(new QUdpSocket(this))
    , networkManager(new QNetworkAccessManager(this))
    , timeoutTimer(new QTimer(this))
    , localAddress(local) {
    connect(udpSocket, &QUdpSocket::readyRead, this, &UPnPConnector::onUdpReadyRead);
    connect(networkManager, &QNetworkAccessManager::finished, this, &UPnPConnector::onHttpFinished);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, &UPnPConnector::onTimeout);
}

UPnPConnector::~UPnPConnector() {
    // Cleanup is managed by QObject parent-child relationships.
}

void UPnPConnector::discoverDevices() {
    std::cout << "UDP socket state before bind: " << udpSocket->state() << std::endl;
    // You may try port 1900 if you want to follow the standard.
    if (!udpSocket->bind(localAddress->ip(), 1901, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred("Failed to bind UDP socket: " + udpSocket->errorString());
        return;
    }

    QString searchRequest =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 5\r\n"
        "ST: upnp:rootdevice\r\n"
        "\r\n";
    std::cout << "Sending SSDP discovery request..." << std::endl;
    udpSocket->writeDatagram(searchRequest.toUtf8(), QHostAddress("239.255.255.250"), 1900);
    timeoutTimer->start(5000);
}

void UPnPConnector::onUdpReadyRead() {
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16      senderPort;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        QString response = QString::fromUtf8(datagram);
        std::cout << "Received UDP response from: " << sender.toString().toStdString() << "port: " << senderPort
                  << std::endl;
        std::cout << "Response:" << response.toStdString() << std::endl;
        int locIndex = response.indexOf("LOCATION: ");
        if (locIndex != -1) {
            int     start    = locIndex + QString("LOCATION: ").length();
            int     end      = response.indexOf("\r\n", start);
            QString location = response.mid(start, end - start).trimmed();
            emit    deviceDiscovered(sender, location);
        }
    }
}

void UPnPConnector::onHttpFinished(QNetworkReply *reply) {
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred("HTTP error: " + reply->errorString());
    } else {
        QString response = QString::fromUtf8(reply->readAll());
        std::cout << "HTTP Response:" << response.toStdString() << std::endl;
        emit soapResponseReceived(response);
        if (currentSoapAction == "GetExternalIPAddress") {
            // Parse the response to extract the external IP.
            // For example, search for <NewExternalIPAddress>...</NewExternalIPAddress>
            int start = response.indexOf("<NewExternalIPAddress>") + QString("<NewExternalIPAddress>").length();
            int end   = response.indexOf("</NewExternalIPAddress>", start);
            if (start > 0 && end > start) {
                QString externalIP = response.mid(start, end - start).trimmed();
                emit    externalIPAddressObtained(externalIP);
            }
        } else if (currentSoapAction == "AddPortMapping") {
            emit portMappingAdded();
        } else if (currentSoapAction == "DeletePortMapping") {
            emit portMappingRemoved();
        }
    }
    reply->deleteLater();
}

void UPnPConnector::onTimeout() {
    qInfo() << "SSDP response timeout reached.";
    udpSocket->close();
    emit errorOccurred("No UPnP device response received within the timeout period.");
}

void UPnPConnector::postSOAP(const QUrl &controlUrl, const QString &soapAction, const QString &message) {
    currentSoapAction = soapAction;
    QNetworkRequest request(controlUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");
    QString soapActionHeader = QString("\"urn:schemas-upnp-org:service:WANIPConnection:1#%1\"").arg(soapAction);
    request.setRawHeader("SOAPACTION", soapActionHeader.toUtf8());
    request.setRawHeader("Content-Length", QString::number(message.toUtf8().size()).toUtf8());
    networkManager->post(request, message.toUtf8());
}

void UPnPConnector::getExternalIPAddress(const QUrl &controlUrl) {
    QString message =
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        " <s:Body>\r\n"
        "  <u:GetExternalIPAddress xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\r\n"
        "  </u:GetExternalIPAddress>\r\n"
        " </s:Body>\r\n"
        "</s:Envelope>\r\n";
    postSOAP(controlUrl, "GetExternalIPAddress", message);
}

void UPnPConnector::addPortMapping(const QUrl    &controlUrl,
                                   int            internalPort,
                                   int            externalPort,
                                   const QString &protocol,
                                   const QString &description,
                                   const QString &internalClient) {
    QString message =
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        " <s:Body>\r\n"
        "  <u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\r\n"
        "   <NewRemoteHost></NewRemoteHost>\r\n"
        "   <NewExternalPort>" + QString::number(externalPort) + "</NewExternalPort>\r\n"
        "   <NewProtocol>" + protocol + "</NewProtocol>\r\n"
        "   <NewInternalPort>" + QString::number(internalPort) + "</NewInternalPort>\r\n"
        "   <NewInternalClient>" + internalClient + "</NewInternalClient>\r\n"
        "   <NewEnabled>1</NewEnabled>\r\n"
        "   <NewPortMappingDescription>" + description + "</NewPortMappingDescription>\r\n"
        "   <NewLeaseDuration>0</NewLeaseDuration>\r\n"
        "  </u:AddPortMapping>\r\n"
        " </s:Body>\r\n"
        "</s:Envelope>\r\n";
    postSOAP(controlUrl, "AddPortMapping", message);
}

void UPnPConnector::removePortMapping(const QUrl &controlUrl, int externalPort, const QString &protocol) {
    QString message =
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        " <s:Body>\r\n"
        "  <u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\r\n"
        "   <NewRemoteHost></NewRemoteHost>\r\n"
        "   <NewExternalPort>" + QString::number(externalPort) + "</NewExternalPort>\r\n"
        "   <NewProtocol>" + protocol + "</NewProtocol>\r\n"
        "  </u:DeletePortMapping>\r\n"
        " </s:Body>\r\n"
        "</s:Envelope>\r\n";
    postSOAP(controlUrl, "DeletePortMapping", message);
}

void UPnPConnector::retrieveDeviceDescription(const QUrl &deviceDescriptionUrl) {
    QNetworkRequest request(deviceDescriptionUrl);
    QNetworkReply  *reply = networkManager->get(request);

    // Allocate a buffer to accumulate data.
    auto *accumulatedData = new QByteArray;

    // Connect readyRead to collect incoming data.
    connect(reply, &QNetworkReply::readyRead, this, [reply, accumulatedData]() {
        *accumulatedData += reply->readAll();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, deviceDescriptionUrl, accumulatedData]() {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Network error:" << reply->errorString();
            emit errorOccurred("Network error: " + reply->errorString());
            reply->deleteLater();
            accumulatedData->clear();
            delete accumulatedData;
            return;
        }

        // QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        // qInfo() << "HTTP status code:" << statusCode.toInt();

        // QByteArray contentLength = reply->rawHeader("Content-Length");
        // qInfo() << "Content-Length:" << contentLength;

        // qInfo() << "Accumulated Data length:" << accumulatedData->size();
        QString xmlContent = QString::fromUtf8(*accumulatedData);
        qInfo() << "Device Description XML:" << xmlContent;

        // Parse the XML description using QXmlStreamReader.
        QXmlStreamReader xml(xmlContent);
        QString          controlUrl;
        bool             foundWANIPConnection = false;
        // Loop over the XML tokens

        while (!xml.atEnd() && !xml.hasError()) {
            xml.readNext();
            if (xml.isStartElement()) {
                // Look for a service element
                if (xml.name() == "service") {
                    QString serviceType;
                    QString serviceControlURL;
                    // Process the inner elements of <service>
                    while (!(xml.isEndElement() && xml.name() == "service")) {
                        xml.readNext();
                        if (xml.isStartElement()) {
                            if (xml.name() == "serviceType") {
                                serviceType = xml.readElementText();
                            } else if (xml.name() == "controlURL") {
                                serviceControlURL = xml.readElementText().trimmed();
                            }
                        }
                    }
                    // Check if this service is WANIPConnection.
                    if (serviceType.contains("WANIPConnection", Qt::CaseInsensitive)) {
                        controlUrl           = serviceControlURL;
                        foundWANIPConnection = true;
                        break;
                    }
                }
            }
        }

        if (foundWANIPConnection && !controlUrl.isEmpty()) {
            // If the control URL is relative, resolve it against the base device URL.
            QUrl baseUrl        = deviceDescriptionUrl;
            QUrl fullControlUrl = baseUrl.resolved(QUrl(controlUrl));
            std::cout << "Found WANIPConnection control URL: " << fullControlUrl.toString().toStdString()
                      << std::endl;
            // emit soapResponseReceived(fullControlUrl.toString());
            emit controlURLFound(fullControlUrl.toString());
        } else {
            emit errorOccurred("Control URL for WANIPConnection not found in device description.");
        }

        delete accumulatedData;
        reply->deleteLater();
    });
}

void UPnPConnector::getSpecificPortMappingEntry(const QUrl    &controlUrl,
                                                int            externalPort,
                                                const QString &protocol) {
    // Construct the SOAP message.
    QString soapMessage =
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "  <s:Body>\r\n"
        "    <u:GetSpecificPortMappingEntry xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">\r\n"
        "      <NewRemoteHost></NewRemoteHost>\r\n"
        "      <NewExternalPort>" + QString::number(externalPort) + "</NewExternalPort>\r\n"
        "      <NewProtocol>" + protocol + "</NewProtocol>\r\n"
        "    </u:GetSpecificPortMappingEntry>\r\n"
        "  </s:Body>\r\n"
        "</s:Envelope>\r\n";

    // Set up the network request.
    QNetworkRequest request(controlUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");

    // Construct the SOAPACTION header.
    QString soapAction = "\"urn:schemas-upnp-org:service:WANIPConnection:1#GetSpecificPortMappingEntry\"";
    request.setRawHeader("SOAPACTION", soapAction.toUtf8());

    request.setRawHeader("Content-Length", QString::number(soapMessage.toUtf8().size()).toUtf8());

    // Send the SOAP request.
    QNetworkReply *reply = networkManager->post(request, soapMessage.toUtf8());

    // Connect to the finished signal to process the response.
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("GetSpecificPortMappingEntry error: " + reply->errorString());
        } else {
            QString response = QString::fromUtf8(reply->readAll());
            qDebug() << "GetSpecificPortMappingEntry SOAP response:" << response;
            // You can parse the response here to verify the mapping details.
            emit soapResponseReceived(response);
        }
        reply->deleteLater();
    });
}
