#include "dfs/packages/headers/req_frags_message.h"

bool DistFileSystem::ReqFragsMessage::isEmpty() const
{
    if (filePath.isEmpty())
        return true;
    if (listFrag.isEmpty())
        return true;
    return false;
}

const QList<QByteArray> DistFileSystem::ReqFragsMessage::serializedParams() const
{
    QList<QByteArray> list;
    list << filePath.toUtf8() << listFrag;
    return list;
}

void DistFileSystem::ReqFragsMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short DistFileSystem::ReqFragsMessage::getFieldsCount() const
{
    return ReqFragsMessage::FIELDS_COUNT;
}

QByteArray DistFileSystem::ReqFragsMessage::serialize() const
{
    return Serialization::universalSerialize({ filePath.toLocal8Bit(), listFrag },
                                             DistFileSystem::fieldsSize);
}

void DistFileSystem::ReqFragsMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized, DistFileSystem::fieldsSize);
    filePath = l.takeFirst();
    listFrag = l.takeFirst();
}
