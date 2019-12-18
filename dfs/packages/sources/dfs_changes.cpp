#include "dfs/packages/headers/dfs_changes.h"

DFSMessage::DfsChanges::DfsChanges()
    : DUMessage(dfsMessageType::changesMessage)
{
}

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
    changeType = list.takeFirst().toInt();
    userId = list.takeFirst();
    signature = list.takeFirst();
}

DFSMessage::DfsChanges::DfsChanges(const QString &filePath, const QByteArrayList &data, const QString &range,
                                   int type, const QByteArray &actorId, const QByteArray &signature)
    : DUMessage(dfsMessageType::changesMessage)
{
    this->filePath = filePath;
    this->data = data;
    this->range = range.toLatin1();
    this->changeType = type;
    this->userId = actorId;
    this->signature = signature;
}

bool DFSMessage::DfsChanges::isEmpty() const
{
    return type == dfsMessageType::none || filePath.isEmpty() || data.isEmpty() || range.isEmpty()
        || changeType == -1 || userId.isEmpty() || signature.isEmpty();
}

const QList<QByteArray> DFSMessage::DfsChanges::serializedParams() const
{
    QList<QByteArray> list;

    list << QByteArray::number(type) << filePath.toUtf8()
         << Serialization::universalSerialize(data, DFSMessage::fieldsSize) << range
         << QByteArray::number(changeType) << userId << signature;

    return list;
}

DFSMessage::DfsChanges DFSMessage::DfsChanges::operator=(const DFSMessage::DfsChanges &msg)
{
    this->filePath = msg.filePath;
    this->data = msg.data;
    this->range = msg.range;
    this->changeType = msg.changeType;
    this->userId = msg.userId;
    this->signature = msg.signature;
    return *this;
}
