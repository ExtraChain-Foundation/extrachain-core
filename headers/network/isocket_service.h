#ifndef ISOCKETSERVICE_H
#define ISOCKETSERVICE_H

#include "utils/exc_utils.h"

class NetworkManager;

class EXTRACHAIN_EXPORT SocketService : public QObject
{
    Q_OBJECT

public:
    explicit SocketService(NetworkManager *networkManager, QObject *parent = nullptr);
    SocketService(const SocketService &socket);
    const QString &identifier() const;
    virtual QString protocolString() const = 0;
    virtual Network::Protocol protocol() const = 0;
    virtual bool isActive() const = 0;
    virtual quint16 port() const = 0;
    virtual quint16 serverPort() const = 0;
    const QString &ip() const;
    int bytesCompressed() const;
    int bytesOutgoing() const;
    int bytesIncoming() const;

protected: // methods
    bool checkFirstMessage(const QString &message);
    virtual void closeSocket();
    QByteArray generateFirstMessage();
    QByteArray prepareSendMessage(const QByteArray &message);
    QByteArray prepareReceiveMessage(const QByteArray &message);

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData);
    void close();
    void activated();
    void finished(); // if threads

protected:
    NetworkManager *m_networkManager = nullptr;
    QString m_identifier;
    QString m_ip;
    bool m_activated = false;
    int m_bytesIncoming = 0;
    int m_bytesOutgoing = 0;
    int m_bytesCompressed = 0;

private:
    QByteArray priv;
    QByteArray pub;
};

#endif // WEBSOCKETSERVICE_H
