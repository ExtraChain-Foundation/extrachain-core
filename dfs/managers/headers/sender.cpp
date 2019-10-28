#include "sender.h"

Sender::Sender(const QByteArray &userId, QObject *parent)
    : QObject(parent)
{
    this->userId = userId;
}
void Sender::process()
{
}
void Sender::sendFile(const QString &filePath, const based_dfs_struct::Type &type, const SocketPair &receiver)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    // create title_message
    int pckgN = 0; // package number
    Message::title_message title(filePath);
    title.f_type = based_dfs_struct::toByteArray(type);
    if (title.empty())
    {
        qDebug() << "empty title";
        return;
    }
    if (receiver.isEmpty())
        emit sendS(title.serialize(), Messages::DFS_MESSAGE);
    else
        emit sendToPeer(title.serialize(), Messages::DFS_MESSAGE, receiver);
    titleHashs.insert(title.hash(), filePath);
    serializedTitle.insert(filePath, title.serialize());
    this->thread()->sleep(3);
    if (file.size() < data_offset + 1)
    {
        QByteArray data = file.read(data_offset);
        // create package
        Message::dfs_message pck(title.hash(), pckgN, data); // package for send
        if (receiver.isEmpty())
            emit sendS(pck.serialize(), Messages::DFS_MESSAGE); // send to Resolver Manager for reqister
        else
            emit sendToPeer(pck.serialize(), Messages::DFS_MESSAGE, receiver);

        qDebug() << "[&sender] send small file";
        return;
    }
    // prepare and send data
    for (pckgN = 0; pckgN < title.pckgsAmount; pckgN++)
    {
        // First step read offset data from file
        QByteArray data = file.read(data_offset);
        // create package
        Message::dfs_message pck(title.hash(), pckgN, data); // package for send
                                                             //        pckgN++;
                                                             //        if ((pckgN % 100) == 0)
                                                             //            this->thread()->sleep(1);

        if (receiver.isEmpty())
            emit sendS(pck.serialize(), Messages::DFS_MESSAGE); // send to Resolver Manager for reqister
        else
            emit sendToPeer(pck.serialize(), Messages::DFS_MESSAGE, receiver);
    }
    // create last package
    QByteArray data = file.read(file.size() - file.pos());
    Message::dfs_message pck(title.hash(), pckgN, data); // package for send
    if (receiver.isEmpty())                              // check condition

        emit sendS(pck.serialize(), Messages::DFS_MESSAGE); // send to Resolver Manager for reqister
    else
        sendToPeer(pck.serialize(), Messages::DFS_MESSAGE, receiver); // response request
    file.close();
}

void Sender::checkClosing(const QByteArray &titleHash, const long long &pckAF, const SocketPair &receiver)
{
    if (titleHashs.find(titleHash) == titleHashs.end())
    {
        qDebug() << "I don't send this file";
        return;
    }
    if (serializedTitle.find(titleHashs[titleHash]) == serializedTitle.end())
    {
        qDebug() << "so it's not so good";
        return;
    }
    Message::title_message tmpTitle(serializedTitle[titleHashs[titleHash]]);
    serializedTitle.erase(serializedTitle.find(titleHashs[titleHash]));
    titleHashs.erase(titleHashs.find(titleHash));
    sendFile(titleHashs[titleHash], based_dfs_struct::convertToDFType(tmpTitle.f_type), receiver);

    qDebug() << "reapeat send for" << tmpTitle.filePath << "file because receiver have" << pckAF << "from"
             << tmpTitle.pckgsAmount;
}
