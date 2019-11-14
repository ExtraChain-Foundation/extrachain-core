#include "dfs/packages/headers/message_struct.h"

DFSMessage::dfs_message::dfs_message(const QByteArray &hash, const long long &pckgNumber,
                                     const QByteArray &data)
    : DUMessage(type_dfs_message)
{
    this->title_hash = hash;
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
    title_hash = list.takeFirst();
    pckgNumber = list.takeFirst().toLong();
    data = list.takeFirst();
}

DFSMessage::dfs_message::dfs_message(const DFSMessage::dfs_message &temp)
    : DUMessage(type_dfs_message)
{
    title_hash = temp.title_hash;
    pckgNumber = temp.pckgNumber;
    data = temp.data;
}

DFSMessage::dfs_message::~dfs_message()
{
}

const QList<QByteArray> DFSMessage::dfs_message::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << title_hash << QByteArray::number(static_cast<long long>(pckgNumber))
         << data;
    return list;
}
