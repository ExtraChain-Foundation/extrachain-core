#ifndef UPNPCONNECTOR_H
#define UPNPCONNECTOR_H

#include <QObject>
#include <QUdpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QUrl>
#include <QHostAddress>

class UPnPConnector : public QObject {
    Q_OBJECT
public:
    explicit UPnPConnector(std::shared_ptr<QNetworkAddressEntry> local, QObject *parent = nullptr);
    ~UPnPConnector();

           // Method to start a test UPnP request (SSDP discovery)
    void testRequest();

           // You could add more methods here, for example:
           // void sendSOAPRequest(const QUrl &url, const QString &soapAction, const QString &message);
           // void parseSOAPResponse(const QString &response);

signals:
    // Emitted when a UPnP device is discovered.
    void deviceDiscovered(const QHostAddress &address, const QString &location);

    // Emitted when a SOAP response is received.
    void soapResponseReceived(const QString &response);

    // Emitted if an error occurs.
    void errorOccurred(const QString &errorMessage);

private slots:
    // Slot to handle incoming UDP responses.
    void onUdpReadyRead();

    // Slot to handle finished HTTP requests (if used for SOAP, etc.)
    void onHttpFinished(QNetworkReply *reply);

    // Timeout slot for handling response timeouts.
    void onTimeout();

private:
    QUdpSocket            *udpSocket;
    QNetworkAccessManager *networkManager;
    QTimer                *timeoutTimer;
    std::shared_ptr<QNetworkAddressEntry> localAddress;
};

#endif // UPNPCONNECTOR_H
