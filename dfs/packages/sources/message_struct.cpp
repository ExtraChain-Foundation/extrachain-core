#include "dfs/packages/headers/message_struct.h"

Message::dfs_message::dfs_message(const QByteArray &hash, const long long &pckgNumber, const QByteArray &data)
    : IDfs_Message(m_type)
{
    this->title_hash = hash;
    this->pckgNumber = pckgNumber;
    this->data = data;
}

Message::dfs_message::dfs_message(const QByteArray &serialized)
    : IDfs_Message(m_type)
{
    QList<QByteArray> list = deserialize(serialized);
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
    : IDfs_Message(m_type)
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
    list << title_hash << QByteArray::number(pckgNumber) << data;
    return list;
}
