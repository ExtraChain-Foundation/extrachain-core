
#include "dfs/packages/headers/dfs_request.h"

DFSMessage::DfsRequest::DfsRequest(const QString &filePath)
    : DUMessage(dfsMessageType::requestMessage)
{
    this->filePath = filePath;
}

DFSMessage::DfsRequest::DfsRequest(const QByteArray &serialized)
    : DUMessage(dfsMessageType::requestMessage)
{
    QList<QByteArray> list = deserialize(serialized);

    if (list.size() != FIELDS_COUNT + 1)
    {
        qDebug() << "Request message struct: incorrect input data";
        return;
    }
    if (dfsMessageType::requestMessage != list.takeFirst().toInt())
    {
        qDebug() << "[DfsRequest] incorrect message type";
    }

    filePath = QString::fromUtf8(list.takeFirst());
}

bool DFSMessage::DfsRequest::isEmpty() const
{
    return type == dfsMessageType::none || filePath.isEmpty();
}

const QList<QByteArray> DFSMessage::DfsRequest::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << filePath.toUtf8();
    return list;
}
