#ifndef SERVER_SERVICE_H
#define SERVER_SERVICE_H

#include <QObject>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <QNetworkAddressEntry>
#include <QThread>
#include <iostream>
//#include "resolver_service.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "utils/utils.h"

/**
 * @brief ServerService is responsible for handling incoming sockets connections.
 * All connections is stored in a sockets list.
 */
class ServerService : public QTcpServer
{
    Q_OBJECT
private:
    // bool active = false;
    QNetworkAddressEntry *localAddress;
    quint16 port;

public:
    ServerService(quint16 networkPort, QNetworkAddressEntry *local, QTcpServer *parent = nullptr);
    ~ServerService() override;

public slots:
    // void run() override;
    int process();

public:
    void startListen();
    bool isActive() const;

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    /**
     * @brief Initial processing of incoming messages.
     * Reads message from socket and emits MessageReceived signal.
     */
    //    void socketMsg();
    void socketDisconnected();

signals:
    void MessageReceived(QByteArray msg, QHostAddress peerAddress);
    void finished();
    // void newSocketAdd(QTcpSocket *addSocket);
    void newConnection(qint64 socketDescriptor);
    void serverStatus(bool socketError);
};
#endif // SERVER_SERVICE_H
