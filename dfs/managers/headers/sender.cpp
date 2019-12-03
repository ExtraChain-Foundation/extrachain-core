#include "sender.h"

void Sender::setNetManager(DFSNetManager *value)
{
    NetManager = value;
}

Sender::Sender(const QByteArray &userId, QObject *parent)
    : QObject(parent)
{
    this->userId = userId;
    //    connect(this, &Sender::resendFragments, this, &Sender::resendFragmentsSlot);
}

void Sender::resendFragments /*Slot*/ (QString path, dfsStruct::Type type, QList<QByteArray> frags)
{
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
    {
        DFSMessage::title_message title(path);
        title.f_type = dfsStruct::toByteArray(type);
        //        qDebug() << "ReSeNd" << path << title.hash();
        std::vector<long long> fragsID;
        foreach (QByteArray b, frags)
        {
            fragsID.push_back(b.toLongLong());
        }
        for (unsigned int i = 0; i < fragsID.size(); i++)
        {
            file.seek(fragsID[i] * data_offset);
            QByteArray data = file.read(data_offset);
            DFSMessage::dfs_message pck(title.dataHash, fragsID[i], data); // package for send
            NetManager->send(pck.serialize(), Messages::DFS_MESSAGE);
            //            QThread::currentThread()->usleep(50);
        }
    }
}
void Sender::process()
{
}
void Sender::sendFile(const QString &filePath, const dfsStruct::Type &type, const SocketPair &receiver)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    // create title_message
    //    unsigned long pckgN = 0; // package number
    DFSMessage::title_message title(filePath);
    title.f_type = dfsStruct::toByteArray(type);
    if (title.empty())
    {
        qDebug() << "empty title";
        return;
    }
    qDebug() << "DataHash from title:" << title.dataHash;
    NetManager->send(title.serialize(), Messages::DFS_MESSAGE, receiver);
    //    titleHashs.insert(title.dataHash, filePath);
    //    serializedTitle.insert(filePath, title.serialize());
    //    QThread::currentThread()->msleep(500);
    //    // prepare and send data
    //    for (pckgN = 0; pckgN < title.pckgsAmount; pckgN++)
    //    {
    //        // First step read offset data from file
    //        QByteArray data = file.read(data_offset);
    //        // create package
    //        DFSMessage::dfs_message pck(title.dataHash, pckgN, data); // package for send
    //        //        emit sendPckg(pck.serialize(), Messages::DFS_MESSAGE, receiver);
    //        NetManager->send(pck.serialize(), Messages::DFS_MESSAGE, receiver);
    //        //        QThread::currentThread()->usleep(50);
    //    }
    //    // create last package
    //    QByteArray data = file.read(file.size() - file.pos());
    //    DFSMessage::dfs_message pck(title.dataHash, pckgN, data); // package for send
    //    NetManager->send(pck.serialize(), Messages::DFS_MESSAGE, receiver);
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
    sendFile(titleHashs[titleHash], dfsStruct::convertToDFType(tmpTitle.f_type), receiver);

    qDebug() << "repeat send for" << tmpTitle.filePath << "file because receiver have" << pckAF << "from"
             << tmpTitle.pckgsAmount;
}
