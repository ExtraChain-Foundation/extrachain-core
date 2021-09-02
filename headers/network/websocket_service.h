#ifndef WEBSOCKETSERVICE_H
#define WEBSOCKETSERVICE_H

#include <QWebSocket>
#include "network/socket_pair.h"
#include "utils/exc_utils.h"

class NetworkManager;
class ActorIndex;

class WebSocketService : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketService(QWebSocket *ws, NetworkManager *newNetworkManager, QObject *parent = nullptr);
    WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket *socket() const;
    const QString &identifier() const;
    bool isActive() const;
    void open(const QUrl &url);

    bool operator==(const WebSocketService &service) const;

    const QString &ip() const;
    quint16 port() const;
    quint16 serverPort() const;
    QString protocolString() const;
    Network::Protocol protocol() const;

    int bytesIncoming() const;
    int bytesOutgoing() const;
    int bytesCompressed() const;

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
    void closeSocket();

    NetworkManager *networkManager = nullptr;
    QWebSocket *m_ws = nullptr;
    QString m_identifier;
    QString m_ip;
    bool activated = false;

    int m_bytesIncoming = 0;
    int m_bytesOutgoing = 0;
    int m_bytesCompressed = 0;
};

#endif // WEBSOCKETSERVICE_H
