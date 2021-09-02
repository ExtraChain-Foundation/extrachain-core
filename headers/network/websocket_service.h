#ifndef WEBSOCKETSERVICE_H
#define WEBSOCKETSERVICE_H

#include <QWebSocket>
#include "network/isocket_service.h"
#include "network/socket_pair.h"
#include "utils/exc_utils.h"

class NetworkManager;
class ActorIndex;

class WebSocketService : public SocketService
{
    Q_OBJECT

public:
    explicit WebSocketService(QWebSocket *ws, NetworkManager *networkManager, QObject *parent = nullptr);
    WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket *socket() const;
    bool isActive() const;
    void open(const QUrl &url);
    virtual QString protocolString() const override
    {
        return "WebSocket";
    }
    virtual Network::Protocol protocol() const override
    {
        return Network::Protocol::WebSocket;
    }

    bool operator==(const WebSocketService &service) const;

    quint16 port() const;
    quint16 serverPort() const;

private slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);
    void sendMessage(const QByteArray &data);
    void onSocketError(QAbstractSocket::SocketError error);

private:
    void connections();
    void sendFirstMessage();
    void closeSocket() override;

    QWebSocket *m_ws = nullptr;
};

#endif // WEBSOCKETSERVICE_H
