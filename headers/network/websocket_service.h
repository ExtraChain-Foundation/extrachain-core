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
    ~WebSocketService() = default;

    QWebSocket *ws() const;

    void send(const QByteArray &data);

signals:

private:
    QWebSocket *m_ws;
};

#endif // WEBSOCKETSERVICE_H
