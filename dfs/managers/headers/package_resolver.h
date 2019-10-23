#ifndef PACKAGE_RESOLVER_H
#define PACKAGE_RESOLVER_H

#include "dfs/managers/headers/dfsindex.h"

#include "dfs/packages/headers/dfs_message_interface.h"

class DFSResolver : public QObject
{
    Q_OBJECT

private:
    DFSResolver(QObject *parent = nullptr);
    ~DFSResolver();

    bool active = false;
    QByteArray msg;
    QByteArray hash;

public:
    DFSResolver(AccountController *account);

    void sendFile(const QString &fileName)
    //??
    {
    }
    void validate();
    bool isActive() const;
    void recieveMsg(const QByteArray &msgS, const SocketPair &receiver);
signals:

    void finished();

    void sendMsg(const QByteArray &data, const QByteArray &msgType);

public slots:
    void process()
    {
    }
};

#endif // PACKAGE_RESOLVER_H
////
// For cpp file:
DFSResolver::DFSResolver(QObject *parent)
    : QObject(parent)
{
}
DFSResolver::~DFSResolver()
{
    emit finished();
}
bool DFSResolver::isActive() const
{
    return active;
}
void DFSResolver::validate()
{
}

void DFSResolver::recieveMsg(const QByteArray &msgS, const SocketPair &receiver)
{

    int msgType = 0; /*msgS.getMsgType()*/ //????
    if (msgType == Message::dfsMessageType::titleMessage)
    {
    }
    else if (msgType == Message::dfsMessageType::statusMessage)
    {
    }
    else if (msgType == Message::dfsMessageType::requestMessage)
    {
    }
    else if (msgType == Message::dfsMessageType::storageMessage)
    {
    }
    else if (msgType == Message::dfsMessageType::fileDataMessage)
    {
    }
    else if (msgType == Message::dfsMessageType::responseMessage)
    {
    }
}
