#ifndef WEBSOCKETSERVICE_H
#define WEBSOCKETSERVICE_H

#include <QObject>
#include <QWebSocket>

class WebSocketService : public QObject
{
    Q_OBJECT

public:
    explicit WebSocketService(QWebSocket *ws, QObject *parent = nullptr);
    WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket *ws() const;

    bool operator==(const WebSocketService &service);

signals:
    void send(const QByteArray &data);
    void disconnected(WebSocketService *service); // TODO

public slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);
    void onSend(const QByteArray &data);

private:
    QWebSocket *m_ws;
    bool active = false;
};

#endif // WEBSOCKETSERVICE_H
