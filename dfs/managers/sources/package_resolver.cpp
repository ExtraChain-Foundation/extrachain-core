#include "dfs/managers/headers/package_resolver.h"
DFSResolver::DFSResolver(QObject *parent)
    : QObject(parent)
{
}
DFSResolver::~DFSResolver()
{
    emit finished();
}

void DFSResolver::sendFile(const QString &fileName)
{
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

    Message::dfsMessageType msgType;
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
