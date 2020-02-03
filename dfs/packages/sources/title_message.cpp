#include "dfs/packages/headers/title_message.h"

bool DistFileSystem::titleMessage::isEmpty() const
{
    if (filePath.isEmpty())
        return true;
    if (pckgsAmount == 0)
        return true;
    if (fileSize == 0)
        return true;
    if (dataHash.isEmpty())
        return true;
    // if (prevId.isEmpty())
    //    return true;
    return false;
}

void DistFileSystem::titleMessage::calcHash()
{
    QFile file(filePath);
    if (file.exists())
    {
        file.open(QIODevice::ReadOnly);
        fileSize = file.size();
        while (file.pos() + dataSize < file.size())
        {
            pckgsAmount++;
            QByteArray sgmHash = Utils::calcKeccak(file.read(dataSize));
            dataHash = Utils::calcKeccak(dataHash + sgmHash);
        }
        pckgsAmount++;
        QByteArray sgmHash = Utils::calcKeccak(file.read(file.size() - file.pos()));
        dataHash = Utils::calcKeccak(dataHash + sgmHash);
        file.close();
    }
}

const QList<QByteArray> DistFileSystem::titleMessage::serializedParams() const
{
    QList<QByteArray> list;
    list << filePath.toUtf8() << QByteArray::number(static_cast<long long>(pckgsAmount))
         << QByteArray::number(fileSize) << dataHash << QByteArray::number(f_type) << prevId;
    return list;
}

void DistFileSystem::titleMessage::operator=(DistFileSystem::TitleMessage title)
{
    filePath = title.filePath;
    pckgsAmount = title.pckgsAmount;
    fileSize = title.fileSize;
    f_type = title.f_type;
    dataHash = title.dataHash;
    prevId = title.prevId;
}

void DistFileSystem::titleMessage::operator=(const QByteArray &serialized)
{
    deserialize(serialized);
}

void DistFileSystem::titleMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short DistFileSystem::titleMessage::getFieldsCount() const
{
    return titleMessage::FIELDS_COUNT;
}

QByteArray DistFileSystem::titleMessage::serialize() const
{
    return Serialization::universalSerialize(serializedParams(), DistFileSystem::fieldsSize);
}

void DistFileSystem::titleMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized);

    if (l.size() != FIELDS_COUNT)
        return;

    filePath = l.takeFirst();
    pckgsAmount = l.takeFirst().toULong();
    fileSize = l.takeFirst().toLongLong();
    dataHash = l.takeFirst();
    f_type = l.takeFirst().toUInt();
    prevId = l.takeFirst();
}
