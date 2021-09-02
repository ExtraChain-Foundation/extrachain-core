#ifndef ISOCKETSERVICE_H
#define ISOCKETSERVICE_H

#include "utils/exc_utils.h"
#include "datastorage/actor.h"

class NetworkManager;
class ActorIndex;

class SocketService : public QObject
{
    Q_OBJECT

public:
    explicit SocketService(NetworkManager *networkManager, QObject *parent = nullptr);
    SocketService(const SocketService &socket);
    const QString &identifier() const;
    virtual QString protocolString() const;
    virtual Network::Protocol protocol() const;
    const QString &ip() const;
    int bytesCompressed() const;
    int bytesOutgoing() const;
    int bytesIncoming() const;

protected:
    NetworkManager *m_networkManager = nullptr;
    QString m_identifier;
    QString m_ip;
    bool m_activated = false;
    int m_bytesIncoming = 0;
    int m_bytesOutgoing = 0;
    int m_bytesCompressed = 0;

protected: // methods
    bool checkFirstMessage(const QString &message);
    virtual void closeSocket();
    QByteArray generateFirstMessage();

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData);
    void close();
};

#endif // WEBSOCKETSERVICE_H
