#include "dfs/packages/headers/message_struct.h"

DFSMessage::dfs_message::dfs_message(const QByteArray &hash, const unsigned long &pckgNumber,
                                     const QByteArray &data)
    : DUMessage(type_dfs_message)
{
    this->dataHash = hash;
    this->pckgNumber = pckgNumber;
    this->data = data;
}

DFSMessage::dfs_message::dfs_message(const QByteArray &serialized)
    : DUMessage(type_dfs_message)
{
    QList<QByteArray> list = deserialize(serialized);
    if (type_dfs_message != list.takeFirst().toInt())
    {
        qDebug() << "[dfs_message]"
                 << "incorrect message type";
    }
    if (list.size() != FIELDS_COUNT)
    {
        qDebug() << "[&Message::dfs_message_struct] incorrect list size";
        return;
    }
    dataHash = list.takeFirst();
    pckgNumber = list.takeFirst().toULong();
    data = list.takeFirst();
}

DFSMessage::dfs_message::dfs_message(const DFSMessage::dfs_message &temp)
    : DUMessage(type_dfs_message)
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
