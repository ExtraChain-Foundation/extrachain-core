
#include "dfs/packages/headers/dfs_request.h"

Message::dfs_request::dfs_request(const QString &filePath, const QByteArray &asker)
    : IDfs_Message(m_type)
{
    this->filePath = filePath;
    this->asker = asker;
}

Message::dfs_request::dfs_request(const QByteArray &serialized)
    : IDfs_Message(m_type)
{
    QList<QByteArray> list = deserialize(serialized);
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
    list << filePath.toUtf8() << asker;
    return list;
}
