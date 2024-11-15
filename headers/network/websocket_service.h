#ifndef WEBSOCKETSERVICE_H
#define WEBSOCKETSERVICE_H

#include "managers/extrachain_node.h"
#include "network/isocket_service.h"
#include "network/network_manager.h"
#include "utils/exc_utils.h"
#include <QWebSocket>

#include "extrachain_global.h"

class EXTRACHAIN_EXPORT WebSocketService : public SocketService {
    Q_OBJECT

public:
    explicit WebSocketService(
        QWebSocket     *ws,
        ExtraChainNode *node,
        QObject        *parent       = nullptr,
        const bool      isConstant   = false,
        const bool      needToDelete = false);
    // WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket     *socket() const;
    bool            isActive() const override;
    void            open(const QString &ip, quint16 port);
    virtual QString protocolString() const override {
        return "WebSocket";
    }
    virtual Network::Protocol protocol() const override {
        return Network::Protocol::WebSocket;
    }

    bool operator==(const WebSocketService &service) const;

    quint16 port() const override;
    quint16 serverPort() const override;

public:
    virtual void sendMessage(const QByteArray &data) override;
    virtual void final() override;

signals:
    void sendMessageInternal(const QByteArray &data);

private slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);

    void onConnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void closeSocket() override;
    void sendMessageInternalSlot(const QByteArray &data);

private:
    void connections();
    void handshake();

    QWebSocket *m_ws = nullptr;

    QTimer m_timer;
};

#endif // WEBSOCKETSERVICE_H
