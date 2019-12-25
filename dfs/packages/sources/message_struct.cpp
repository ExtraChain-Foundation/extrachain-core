#include "dfs/packages/headers/message_struct.h"

DFSMessage::dfs_message::dfs_message()
{
    this->data = "";
    this->dataHash = "";
    this->pckgNumber = ULONG_MAX;
}

DFSMessage::dfs_message::dfs_message(const QByteArray &hash, const size_t &pckgNumber, const QByteArray &data)
    : DUMessage(dfsMessageType::fileDataMessage)
{
    this->dataHash = hash;
    this->pckgNumber = pckgNumber;
    this->data = data;
}

DFSMessage::dfs_message::dfs_message(const QByteArray &serialized)
    : DUMessage(dfsMessageType::fileDataMessage)
{
    QList<QByteArray> list = deserialize(serialized);
    if (list.size() != FIELDS_COUNT + 1)
    {
        qDebug() << "[&Message::dfs_message_struct] incorrect list size";
        return;
    }

    if (dfsMessageType::fileDataMessage != list.takeFirst().toInt())
    {
        qDebug() << "[dfs_message]"
                 << "incorrect message type";
    }

    dataHash = list.takeFirst();
    pckgNumber = list.takeFirst().toLongLong();
    data = list.takeFirst();
}

DFSMessage::dfs_message::dfs_message(const DFSMessage::dfs_message &temp)
    : DUMessage(dfsMessageType::fileDataMessage)
{
    dataHash = temp.dataHash;
    pckgNumber = temp.pckgNumber;
    data = temp.data;
}

DFSMessage::dfs_message::~dfs_message()
{
}

const QList<QByteArray> DFSMessage::dfs_message::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << dataHash << QByteArray::number(static_cast<long long>(pckgNumber))
         << data;
    return list;
}
