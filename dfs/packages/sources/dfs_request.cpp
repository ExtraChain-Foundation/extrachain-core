
#include "dfs/packages/headers/dfs_request.h"

Message::dfs_request::dfs_request(const QString &filePath, const QByteArray &asker)
    : DUMessage(type_dfs_request)
{
    this->filePath = filePath;
    this->asker = asker;
}

Message::dfs_request::dfs_request(const QByteArray &serialized)
    : DUMessage(type_dfs_request)
{

    QList<QByteArray> list = deserialize(serialized);
    if (type_dfs_request != list.takeFirst().toInt())
    {
        qDebug() << "[dfs_request]"
                 << "incorrect message type";
    }
    if (list.size() != FIELDS_COUNT)
    {
        qDebug() << "request_message_struct << incorrect input data";
        return;
    }
    filePath = QString::fromUtf8(list.takeFirst());
    asker = list.takeFirst();
}

Message::dfs_request::~dfs_request()
{
}

const QList<QByteArray> Message::dfs_request::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << filePath.toUtf8() << asker;
    return list;
}
