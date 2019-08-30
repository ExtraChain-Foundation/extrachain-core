#ifndef SOCKET_SERVICE_H
#define SOCKET_SERVICE_H

#include <QObject>
#include <QtNetwork/QTcpSocket>
#include <QHostAddress>
#include <QThread>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QMetaType>
#include "datastorage/transaction.h"
#include "datastorage/block.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "network/resolver_service.h"
#include <QTimer>

#include "network/socket_pair.h"

using namespace SearchEnum;

/**
 * @brief SocketService is responsible for message delivery
 */
class SocketService : public QObject
{
    Q_OBJECT
private:
    int connectionTry = 0;
    qintptr socketDescriptor = 0;
    bool active = false;
    QString address;
    quint16 port;
    QTcpSocket *socket = nullptr;
    BigNumber indetificator;
    int _blockSize = 0;
    QByteArray buffer;
    int reconnectTry = 0;

public:
    SocketService(QString address, quint16 networkPort, QObject *parent = nullptr);
    SocketService(qintptr socketDescriptor, QObject *parent = nullptr);
    ~SocketService() override;

signals:
    void MessageReceived(const QByteArray &msgS, const QString &peerAddressst, const int port);
    /**
     * @brief has only one connection with &QTcpSocket::disconnected on client
     * and connection with &NetManager::removeConnection on server
     */
    void clientDisconnected();
    void removeMe();
    void connected();
    void clientRemove();
    void finished();
    void checkMe();
private slots:

    void reconnect();
    void readData();
public slots:
    /**
     * @brief Send message using QTcpSocket
     * @param message
     */
    void sendMsg(const QByteArray &data, const SocketPair &socketData);
    /**
     * @brief stops this thread
     */
    void sockReady();
    void closeSocket();
    void process();
    void establishConnection();

public:
    bool *socketStatus() const;
    bool isActive() const;
    QString getAddress() const;
    quint16 getPort() const;

    QHostAddress getSocketAddress() const;
    quint16 getSocketPeer() const;
    QTcpSocket *getSocket() const;
    void setSocket(QTcpSocket *value);
    QTcpSocket::SocketState state();
    int getReconnectTry() const;
    void setReconnectTry(int value);
    BigNumber getIdentificator() const;
    void setIdentificator(const BigNumber &value);
    bool getActive() const;
};
#endif // SOCKET_SERVICE_H
