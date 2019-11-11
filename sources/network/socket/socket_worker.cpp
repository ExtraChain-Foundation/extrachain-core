#include "headers/network/socket/socket_worker.h"
#include "headers/network/socket_service.h"

SocketWorker::SocketWorker(net::Worker type, QByteArray *buf, QObject *parent)
    : QObject(parent)
{
    this->type = type;
    this->dpBuf = buf;
}

SocketWorker::~SocketWorker()
{
    delete timer;
}

bool SocketWorker::isActive()
{
    return active;
}

void SocketWorker::process()
{
    timer = new QTimer(this);
    if (this->type == net::Worker::Read)
    {
        connect(timer, &QTimer::timeout, this, &SocketWorker::doRead);
    }
    else if (this->type == net::Worker::Add)
    {
        connect(timer, &QTimer::timeout, this, &SocketWorker::doAdd);
    }
    timer->start(5);
    active = true;
}

void SocketWorker::killWorker()
{
    timer->stop();
    if (this->type == net::Worker::Read)
    {
        disconnect(timer, &QTimer::timeout, this, &SocketWorker::doRead);
    }
    else if (this->type == net::Worker::Add)
    {
        disconnect(timer, &QTimer::timeout, this, &SocketWorker::doAdd);
    }
    active = false;
}

void SocketWorker::setSocket(SocketService *value)
{
    socket = value;
}

void SocketWorker::doRead()
{
    if (pendMsgSize > 0)
    {
        continueDoRead();
    }
    else
    {
        if (dpBuf->size() < 4)
        {
            return;
        }
        mutex.lock();
        QByteArray msgLength = dpBuf->mid(0, 4);
        pendMsgSize = Utils::qByteArrayToInt(msgLength);
        dpBuf->remove(0, 4);
        mutex.unlock();
        if (dpBuf->size() >= pendMsgSize)
            continueDoRead();
        else
            return;
    }
}

void SocketWorker::continueDoRead()
{
    mutex.lock();
    QByteArray pckg = dpBuf->mid(0, pendMsgSize);
    dpBuf->remove(0, pendMsgSize);
    pendMsgSize = 0;
    mutex.unlock();
    if (!socket->isActive() && pckg.left(IDENTIFICATOR.size()) == IDENTIFICATOR)
    {
        QByteArray b = pckg.mid(IDENTIFICATOR.size());
        socket->processID(b);
    }
    else
    {
        SocketPair receiver(socket->getAddress().toStdString(), socket->getPort());
        receiver.setId(socket->getID().toByteArray());
        socket->gotMessage(pckg, receiver);
    }
}

void SocketWorker::doAdd()
{
}
