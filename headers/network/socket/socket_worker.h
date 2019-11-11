#ifndef SOCKET_WORKER_H
#define SOCKET_WORKER_H
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace net {
enum Worker
{
    Read = 0,
    Add = 1
};
}
class SocketService;
class SocketWorker : public QObject
{
    Q_OBJECT
private:
    const QByteArray IDENTIFICATOR = "Ind:";
    QByteArray *dpBuf;
    net::Worker type;
    SocketService *socket;
    bool active = false;
    QTimer *timer;
    int pendMsgSize = 0;

public:
    SocketWorker(net::Worker type, QByteArray *buf, QObject *parent = nullptr);
    ~SocketWorker();

public:
    bool isActive();
    void setSocket(SocketService *value);

private:
    void continueDoRead();
private slots:
    void doRead();
    void doAdd();
public slots:
    void process();
    void killWorker();
signals:
    void finished();
};

#endif // SOCKET_WORKER_H
