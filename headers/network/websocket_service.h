#ifndef WEBSOCKETSERVICE_H
#define WEBSOCKETSERVICE_H

#include <QWebSocket>
#include "network/socket_pair.h"

class NetManager;

class WebSocketService : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketService(QWebSocket *ws = nullptr, QObject *parent = nullptr);
    WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket *ws() const;
    const QString &identificator() const;
    bool isActive() const;

    void open(const QUrl &url);
    void close();

    void sendError(int code, const QString &text);

    bool operator==(const WebSocketService &service) const;

    void setNetworkManager(NetManager *newNetworkManager);

    const QString &ip() const;

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(int code, QString errorText);
    void resolveMessage(QByteArray msg, SocketPair receiver);

public slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);
    void sendMessage(const QByteArray &data);

private:
    void connections();
    void sendFirstMessage();
    void parseError(const QString &message);

    NetManager *networkManager = nullptr;
    QWebSocket *m_ws = nullptr;
    QString m_identificator; // TODO: check
    QString m_ip;
    bool activated = false;
};

#endif // WEBSOCKETSERVICE_H
