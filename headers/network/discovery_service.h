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
