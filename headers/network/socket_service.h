#ifndef SOCKET_SERVICE_H
#define SOCKET_SERVICE_H

#ifndef NETWORK_MANAGER_DEF
#define NETWORK_MANAGER_DEF
class NetManager;
#include "headers/network/network_manager.h"
#endif

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
//#include "network/resolver_service.h"
#include <QTimer>

#include "network/socket_pair.h"

using namespace SearchEnum;

/**
 * @brief SocketService is responsible for message delivery
 */
class SocketService : public QObject
{
    Q_OBJECT
    const QByteArray IDENTIFICATOR = "Ind:";

private:
    NetManager *netManager = nullptr;
    int connectionTry = 0;
    qintptr socketDescriptor = 0;
    bool active = false;
    QString address;
    quint16 port;
    QTcpSocket *socket = nullptr;
    BigNumber identificator;
    int _blockSize = 0;
    QByteArray buffer;
    int reconnectTry = 0;

public:
    explicit SocketService(QObject *parent = nullptr);
    explicit SocketService(NetManager *netManager, QObject *parent = nullptr);
    explicit SocketService(const SocketService &value);
    explicit SocketService(NetManager *netManager, QString address, quint16 networkPort,
                           QObject *parent = nullptr);
    explicit SocketService(NetManager *netManager, qintptr socketDescriptor, QObject *parent = nullptr);
    ~SocketService() override;

signals:
    //    void MessageReceived(const QByteArray &msgS, const SocketPair &receiver);
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

    //    void moveMe();
    void setActiveSignal(bool active);
private slots:

    void reconnect();
    void readData();
public slots:

    /**
     * @brief stops this thread
     */
    void sockReady();
    void closeSocket();
    void process();
    void establishConnection();
    void setActive(bool active);

public:
    void setManager(NetManager *nm);

    /**
     * @brief Send message using QTcpSocket
     * @param message
     */
    void sendMsg(const QByteArray &data, const SocketPair &socketData);

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
