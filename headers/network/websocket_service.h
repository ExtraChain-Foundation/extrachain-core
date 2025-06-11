#pragma once
#include "managers/extrachain_node.h"
#include "network/isocket_service.h"
#include "network/network_manager.h"
#include "utils/exc_utils.h"
#include <QWebSocket>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include "extrachain_global.h"

class EXTRACHAIN_EXPORT WebSocketService : public SocketService {
    Q_OBJECT

public:
    explicit WebSocketService(QWebSocket     *ws,
                              ExtraChainNode *node,
                              QObject        *parent      = nullptr,
                              const bool      is_constant = false);
    ~WebSocketService() override;

    QWebSocket               *socket() const;
    bool                      is_active() const override;
    void                      open(const QString &ip, quint16 port);
    QString                   protocol_string() const override;
    Network::Protocol         protocol() const override;
    quint16                   port() const override;
    quint16                   server_port() const override;

    bool operator==(const WebSocketService &service) const;

public slots:
    void send_message(const QByteArray &data, Priority priority = Priority::High) override;
    void flush() override;

signals:
    void sendMessageInternal(const QByteArray &data);
    void needToTryDequeue();
    void closeSocketSig();

private slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);
    void onConnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void closeSocket() override;
    void sendMessageInternalSlot(const QByteArray &data);
    void tryDequeueMessage();

private:
    // Helper methods
    void connections();
    void send_public_key();
    void handshake();
    bool canSendMore() const;
    bool isSocketValid() const;
    bool safeSocketSend(const std::function<qint64()> &sendFunc, const QString &operation);
    void processMessage(const QByteArray &message);
    void processCachedMessages();
    void cleanupSocket();

           // Member variables
    QWebSocket                *m_ws                   = nullptr;
    QTimer                    *m_pingTimer            = nullptr;
    std::atomic<int>           m_failedPongs          {0};
    std::atomic<bool>          m_socketValid          {false};

    // Thread safety
    mutable QMutex             m_sendMutex;
    mutable QMutex             m_stateMutex;

    // Message caching
    std::queue<QByteArray>     m_messageCache;

    // Constants
    static constexpr qint64    MAX_BUFFER_SIZE        = 1024 * 1024; // 1MB
    static constexpr int       MAX_FAILED_PONGS       = 3;
    static constexpr int       PING_INTERVAL_MS       = 3000;
};
