#include "dfs/packages/headers/dfs_changes.h"

DFSMessage::DfsChanges::DfsChanges(const QByteArray &serialized)
    : DUMessage(dfsMessageType::changesMessage)
{
    QList<QByteArray> list = deserialize(serialized);

    if (dfsMessageType::changesMessage != list.takeFirst().toInt())
    {
        qDebug() << "[DfsChanges] Incorrect message type";
    }

    if (list.size() != FIELDS_COUNT)
    {
        qDebug() << "[DfsChanges] Incorrect input data";
        return;
    }

    filePath = QString::fromUtf8(list.takeFirst());
    data = Serialization::universalDeserialize(list.takeFirst(), DFSMessage::fieldsSize);
    range = list.takeFirst();
    type = list.takeFirst().toInt();
    userId = list.takeFirst();
    signature = list.takeFirst();
}

DFSMessage::DfsChanges::DfsChanges(const QString &filePath, const QByteArray &range, int type,
                                   const QByteArray &actorId, const QByteArray &signature,
                                   const QByteArrayList &data)
    : DUMessage(dfsMessageType::changesMessage)
{
    this->filePath = filePath;
    this->data = data;
    this->range = range;
    this->type = type;
    this->userId = actorId;
    this->signature = signature;
}

bool DFSMessage::DfsChanges::empty() const
{
    return filePath.isEmpty() || data.isEmpty() || range.isEmpty() || type == -1 || userId.isEmpty()
        || signature.isEmpty();
}

const QList<QByteArray> DFSMessage::DfsChanges::serializedParams() const
{
    QList<QByteArray> list;

    list << filePath.toUtf8() << Serialization::universalSerialize(data, DFSMessage::fieldsSize) << range
         << QByteArray::number(type) << userId << signature;

    return list;
}

DFSMessage::DfsChanges DFSMessage::DfsChanges::operator=(const DFSMessage::DfsChanges &msg)
{
    this->filePath = msg.filePath;
    this->filePath = msg.filePath;
    this->range = msg.range;
    this->userId = msg.userId;
    this->signature = msg.signature;
    this->data = msg.data;
    return *this;
}
