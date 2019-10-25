#include "dfs/packages/headers/message_struct.h"

Message::dfs_message::dfs_message(const QByteArray &hash, const long long &pckgNumber, const QByteArray &data)
    : DUMessage(type_dfs_message)
{
    this->title_hash = hash;
    this->pckgNumber = pckgNumber;
    this->data = data;
}

Message::dfs_message::dfs_message(const QByteArray &serialized)
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

Message::dfs_message::dfs_message(const Message::dfs_message &temp)
    : DUMessage(type_dfs_message)
{
    title_hash = temp.title_hash;
    pckgNumber = temp.pckgNumber;
    data = temp.data;
}

Message::dfs_message::~dfs_message()
{
}

const QList<QByteArray> Message::dfs_message::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << title_hash << QByteArray::number(pckgNumber) << data;
    return list;
}
