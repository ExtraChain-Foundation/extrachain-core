
#include "dfs/packages/headers/dfs_request.h"

DFSMessage::dfs_request::dfs_request(const QString &filePath, const QByteArray &asker)
    : DUMessage(dfsMessageType::requestMessage)
{
    this->filePath = filePath;
    this->asker = asker;
}

DFSMessage::dfs_request::dfs_request(const QByteArray &serialized)
    : DUMessage(dfsMessageType::requestMessage)
{

    QList<QByteArray> list = deserialize(serialized);
    if (dfsMessageType::requestMessage != list.takeFirst().toInt())
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

DFSMessage::dfs_request::~dfs_request()
{
}

const QList<QByteArray> DFSMessage::dfs_request::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << filePath.toUtf8() << asker;
    return list;
}
