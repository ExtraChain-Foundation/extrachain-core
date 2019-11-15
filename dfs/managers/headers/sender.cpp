#include "sender.h"

void Sender::setNetManager(DFSNetManager *value)
{
    NetManager = value;
}

Sender::Sender(const QByteArray &userId, QObject *parent)
    : QObject(parent)
{
    this->userId = userId;
}

void Sender::resendFragments(QString path, QList<QByteArray> frags)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
    {
        DFSMessage::title_message title(path);
        QByteArray data = file.read(data_offset);
        for (int i = 0; i < frags.size(); i++)
        {
            long long pckgN = frags[i].toLongLong();
            // create package
            DFSMessage::dfs_message pck(title.hash(), pckgN, data); // package for send
            NetManager->send(pck.serialize(), Messages::DFS_MESSAGE);
        }
    }
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
    DFSMessage::title_message title(filePath);
    title.f_type = based_dfs_struct::toByteArray(type);
    if (title.empty())
    {
        qDebug() << "empty title";
        return;
    }
    NetManager->send(title.serialize(), Messages::DFS_MESSAGE, receiver);
    titleHashs.insert(title.hash(), filePath);
    serializedTitle.insert(filePath, title.serialize());
    // prepare and send data
    for (pckgN = 0; pckgN < title.pckgsAmount; pckgN++)
    {
        // First step read offset data from file
        QByteArray data = file.read(data_offset);
        // create package
        DFSMessage::dfs_message pck(title.hash(), pckgN, data); // package for send
        //        emit sendPckg(pck.serialize(), Messages::DFS_MESSAGE, receiver);
        NetManager->send(pck.serialize(), Messages::DFS_MESSAGE, receiver);
    }
    // create last package
    QByteArray data = file.read(file.size() - file.pos());
    DFSMessage::dfs_message pck(title.hash(), pckgN, data); // package for send
    NetManager->send(pck.serialize(), Messages::DFS_MESSAGE, receiver);
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
    DFSMessage::title_message tmpTitle(serializedTitle[titleHashs[titleHash]]);
    serializedTitle.erase(serializedTitle.find(titleHashs[titleHash]));
    titleHashs.erase(titleHashs.find(titleHash));
    sendFile(titleHashs[titleHash], based_dfs_struct::convertToDFType(tmpTitle.f_type), receiver);

    qDebug() << "reapeat send for" << tmpTitle.filePath << "file because receiver have" << pckAF << "from"
             << tmpTitle.pckgsAmount;
}
