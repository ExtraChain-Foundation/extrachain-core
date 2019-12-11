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
    pckgNums = list.takeFirst().split(' ');
    actorId = list.takeFirst();
    signature = list.takeFirst();
    data = Serialization::universalDeserialize(list.takeFirst(), DFSMessage::fieldsSize);
}

DFSMessage::DfsChanges::DfsChanges(const QString &filePath, const QByteArrayList &pckgNums,
                                   const QByteArray &actorId, const QByteArray &signature,
                                   const QByteArrayList &data)
    : DUMessage(dfsMessageType::changesMessage)
{
    this->filePath = filePath;
    this->pckgNums = pckgNums;
    this->actorId = actorId;
    this->signature = signature;
    this->data = data;
}

bool DFSMessage::DfsChanges::empty() const
{
    return filePath.isEmpty() || pckgNums.isEmpty() || actorId.isEmpty() || signature.isEmpty()
        || data.isEmpty();
}

const QList<QByteArray> DFSMessage::DfsChanges::serializedParams() const
{
    QList<QByteArray> list;

    list << QByteArray::number(type) << filePath.toUtf8() << pckgNums.join(' ') << actorId << signature
         << Serialization::universalSerialize(data, DFSMessage::fieldsSize);

    return list;
}

DFSMessage::DfsChanges DFSMessage::DfsChanges::operator=(const DFSMessage::DfsChanges &msg)
{
    this->filePath = msg.filePath;
    this->filePath = msg.filePath;
    this->pckgNums = msg.pckgNums;
    this->actorId = msg.actorId;
    this->signature = msg.signature;
    this->data = msg.data;
    return *this;
}
