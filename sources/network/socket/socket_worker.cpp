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
    //
}

bool SocketWorker::isActive()
{
    return active;
}

void SocketWorker::process()
{
    active = true;
    while (isActive())
    {
        if (this->type == net::Worker::Read)
        {
            doRead();
        }
        else if (this->type == net::Worker::Add)
        {
            doAdd();
        }
    }
}

void SocketWorker::killWorker()
{
    //
}

void SocketWorker::setSocket(SocketService *value)
{
    socket = value;
}

void SocketWorker::doRead()
{
    while (dpBuf->size() < 4)
    {
        QThread::currentThread()->usleep(500);
        //        doRead();
    }
    QByteArray msgLength = dpBuf->mid(0, 4);
    int pckSize = Utils::qByteArrayToInt(msgLength);
    dpBuf->remove(0, 4);
    while (dpBuf->size() < pckSize)
        QThread::currentThread()->usleep(500);
    QByteArray pckg = dpBuf->mid(0, pckSize);
    dpBuf->remove(0, pckSize);
    if (!socket->isActive() && pckg.left(IDENTIFICATOR.size()) == IDENTIFICATOR)
    {
        QByteArray b = pckg.mid(IDENTIFICATOR.size());
        socket->processID(b);
        return;
    }
    else
    {
        SocketPair receiver(socket->getAddress().toStdString(), socket->getPort());
        receiver.setId(socket->getID().toByteArray());
        socket->gotMessage(pckg, receiver);
    }
    if (socket->getSocket()->bytesAvailable())
        doRead();
}

void SocketWorker::doAdd()
{
}
