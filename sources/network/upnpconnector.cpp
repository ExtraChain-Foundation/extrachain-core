#include "network/upnpconnector.h"
#include <QDebug>
#include <QNetworkRequest>

UPnPConnector::UPnPConnector(std::shared_ptr<QNetworkAddressEntry> local, QObject *parent)
    : QObject(parent),
    udpSocket(new QUdpSocket(this)),
    networkManager(new QNetworkAccessManager(this)),
    timeoutTimer(new QTimer(this)),
    localAddress(local)
{
    // Connect the UDP socket readyRead signal to our slot.
    connect(udpSocket, &QUdpSocket::readyRead, this, &UPnPConnector::onUdpReadyRead);

    // Connect network manager's finished signal for potential SOAP requests.
    connect(networkManager, &QNetworkAccessManager::finished, this, &UPnPConnector::onHttpFinished);

    // Configure the timer for handling timeouts.
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, &UPnPConnector::onTimeout);
}

UPnPConnector::~UPnPConnector()
{
  // Clean-up is handled by QObject parent-child mechanism.S
}

void UPnPConnector::testRequest()
{
    QHostAddress localInterface = QHostAddress("10.0.2.15");

    QList<QNetworkInterface> interfacesToPrint = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfacesToPrint) {
        std::cout << "Interface:" << iface.humanReadableName().toStdString()<<std::endl;
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            std::cout << "  Address:" << entry.ip().toString().toStdString()<<std::endl;
        }
    }

    // QNetworkInterface targetInterface;
    // const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    // for (const QNetworkInterface &iface : interfaces) {
    //     for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
    //         if (entry.ip() == localInterface) {
    //             targetInterface = iface;
    //             break;
    //         }
    //     }
    //     if (targetInterface.isValid())
    //         break;
    // }

    // if (!targetInterface.isValid()) {
    //     emit errorOccurred("No valid network interface found for " + localInterface.toString());
    //     return;
    // }

    // Bind UDP socket to port 1900 using shared address options.
    if (!udpSocket->bind(localInterface/*localAddress->ip()*/, 1900, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        emit errorOccurred("Failed to bind UDP socket: " + udpSocket->errorString());
    }

    if (!udpSocket->joinMulticastGroup(QHostAddress("239.255.255.250"), targetInterface)) {
        std::cout << "Failed to join multicast group:" << udpSocket->errorString().toStdString() <<std::endl;
    }
    // This method sends an SSDP M-SEARCH request as a test.
    QString searchRequest =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 5\r\n"               // Maximum wait time in seconds
        "ST: upnp:rootdevice\r\n" // Search target; adjust as needed
        "\r\n";

    // Send the multicast request
    std::cout << "Sending SSDP discovery request..."<<std::endl;
    udpSocket->writeDatagram(searchRequest.toUtf8(), QHostAddress("239.255.255.250"), 1900);

    // Start a timer to wait for responses (e.g., 3 seconds plus a bit extra)
    timeoutTimer->start(5000); // 5 seconds timeout
}

void UPnPConnector::onUdpReadyRead()
{
    // Process incoming UDP datagrams.
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(static_cast<int>(udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

        // For this example, we assume the response contains a LOCATION header.
        QString response = QString::fromUtf8(datagram);
        std::cout << "Received UDP response from:" << sender.toString().toStdString() << "Port:" << senderPort << std::endl;
        std::cout << "Response:" << response.toStdString()<<std::endl;

        // Try to extract a location URL from the response.
        // This is a simple extraction. In production, use a robust parser.
        int locIndex = response.indexOf("LOCATION: ");
        if (locIndex != -1) {
            int start = locIndex + QString("LOCATION: ").length();
            int end = response.indexOf("\r\n", start);
            QString location = response.mid(start, end - start).trimmed();
            emit deviceDiscovered(sender, location);
        }
    }
}

void UPnPConnector::onHttpFinished(QNetworkReply *reply)
{
    // This slot can be used for handling SOAP or HTTP responses.
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred("HTTP error: " + reply->errorString());
    } else {
        QString response = QString::fromUtf8(reply->readAll());
        std::cout << "HTTP Response:" << response.toStdString() <<std::endl;
        emit soapResponseReceived(response);
    }
    reply->deleteLater();
}

void UPnPConnector::onTimeout()
{
    // Called when the timeout timer expires.
    std::cout << "SSDP response timeout reached."<<std::endl;
    emit errorOccurred("No UPnP device response received within the timeout period.");
}
