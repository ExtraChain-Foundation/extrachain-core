#ifndef WEBSOCKETSERVICE_H
#define WEBSOCKETSERVICE_H

#include <QWebSocket>
#include "network/socket_pair.h"
#include "utils/exc_utils.h"

class NetManager;
class ActorIndex;

class WebSocketService : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketService(QWebSocket *ws, NetManager *newNetworkManager, ActorIndex *newActorIndex,
                              QObject *parent = nullptr);
    WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket *socket() const;
    const QString &identifier() const;
    bool isActive() const;
    void open(const QUrl &url);
    void sendError(Network::SocketServiceError code, const QString &errorData);

    bool operator==(const WebSocketService &service) const;

    const QString &ip() const;
    quint16 port() const;
    quint16 serverPort() const;
    QString protocolString() const;
    Network::Protocol protocol() const;

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData);
    void close();

private slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);
    void sendMessage(const QByteArray &data);
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void connections();
    void sendFirstMessage();
    void parseError(const QString &message);
    void closeSocket();

    NetManager *networkManager = nullptr;
    ActorIndex *actorIndex;
    QWebSocket *m_ws = nullptr;
    QString m_identifier;
    QString m_ip;
    bool activated = false;
};

#endif // WEBSOCKETSERVICE_H
