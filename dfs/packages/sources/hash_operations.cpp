#include "dfs/packages/headers/hash_operations.h"

const QList<QByteArray> DistFileSystem::requestLast::serializedParams() const
{
    QList<QByteArray> list;
    list << actors.join(" ");
    return list;
}

void DistFileSystem::requestLast::operator=(QList<QByteArray> &list)
{
    if (list.size() == 1)
    {
        actors = list.takeFirst().split(' ');
    }
}

void DistFileSystem::requestLast::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::requestLast::isEmpty() const
{
    return actors.isEmpty();
}

short DistFileSystem::requestLast::getFieldsCount() const
{
    return requestLast::FIELDS_COUNT;
}

QByteArray DistFileSystem::requestLast::serialize() const
{
    return Serialization::serialize(serializedParams(), 8);
}

void DistFileSystem::requestLast::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, 8);
    operator=(l);
}

// --------------------------------------------------------------------------------

const QList<QByteArray> DistFileSystem::responseLast::serializedParams() const
{
    QList<QByteArray> list;
    list << lasts.join("|");
    return list;
}

void DistFileSystem::responseLast::operator=(QList<QByteArray> &list)
{
    if (list.size() == FIELDS_COUNT)
    {
        lasts = list.takeFirst().split('|');
    }
}

void DistFileSystem::responseLast::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::responseLast::isEmpty() const
{
    return lasts.isEmpty();
}

short DistFileSystem::responseLast::getFieldsCount() const
{
    return responseLast::FIELDS_COUNT;
}

QByteArray DistFileSystem::responseLast::serialize() const
{
    return Serialization::serialize(serializedParams(), 8);
}

void DistFileSystem::responseLast::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, 8);
    operator=(l);
}

// --------------------------------------------------------------------------------

const QList<QByteArray> DistFileSystem::CardFileChange::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(key) << actorId << fileId << prevId << nextId << QByteArray::number(type)
         << QByteArray::number(version) << sign;
    return list;
}

void DistFileSystem::CardFileChange::operator=(QList<QByteArray> &list)
{
    if (list.size() == FIELDS_COUNT)
    {
        key = list.takeFirst().toInt();
        actorId = list.takeFirst();
        fileId = list.takeFirst();
        prevId = list.takeFirst();
        nextId = list.takeFirst();
        type = list.takeFirst().toInt();
        version = list.takeFirst().toInt();
        sign = list.takeFirst();
    }
    else
    {
        qDebug() << "CardFileChange error";
    }
}

void DistFileSystem::CardFileChange::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::CardFileChange::isEmpty() const
{
    return key == -1 || actorId.isEmpty() || fileId.isEmpty() || prevId.isEmpty() || nextId.isEmpty()
        || type == -1 || version == -1 || sign.isEmpty();
}

short DistFileSystem::CardFileChange::getFieldsCount() const
{
    return CardFileChange::FIELDS_COUNT;
}

QByteArray DistFileSystem::CardFileChange::serialize() const
{
    return Serialization::serialize(serializedParams(), 4);
}

void DistFileSystem::CardFileChange::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, 4);
    if (l.length() < FIELDS_COUNT)
    {
        qDebug() << "CardFileChange error";
    }
    operator=(l);
}

// --------------------------------------------------------------------------------

const QList<QByteArray> DistFileSystem::RequestCardPart::serializedParams() const
{
    QList<QByteArray> list;
    list << actorId << QByteArray::number(count) << QByteArray::number(offset);
    return list;
}

void DistFileSystem::RequestCardPart::operator=(QList<QByteArray> &list)
{
    if (list.size() == FIELDS_COUNT)
    {
        actorId = list.takeFirst();
        count = list.takeFirst().toInt();
        offset = list.takeFirst().toInt();
    }
}

void DistFileSystem::RequestCardPart::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::RequestCardPart::isEmpty() const
{
    return actorId.isEmpty() || count == -1 || offset == -1;
}

short DistFileSystem::RequestCardPart::getFieldsCount() const
{
    return RequestCardPart::FIELDS_COUNT;
}

QByteArray DistFileSystem::RequestCardPart::serialize() const
{
    return Serialization::serialize(serializedParams(), 8);
}

void DistFileSystem::RequestCardPart::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, 8);
    operator=(l);
}

// --------------------------------------------------------------------------------

const QList<QByteArray> DistFileSystem::ResponseCardPart::serializedParams() const
{
    QList<QByteArray> list;
    list << actorId << QByteArray::number(count) << QByteArray::number(offset) << data.join('|');
    return list;
}

void DistFileSystem::ResponseCardPart::operator=(QList<QByteArray> &list)
{
    if (list.size() == FIELDS_COUNT)
    {
        actorId = list.takeFirst();
        count = list.takeFirst().toInt();
        offset = list.takeFirst().toInt();
        data = list.takeFirst().split('|');
    }
}

void DistFileSystem::ResponseCardPart::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::ResponseCardPart::isEmpty() const
{
    return actorId.isEmpty() || count == -1 || offset == -1 || data.isEmpty();
}

short DistFileSystem::ResponseCardPart::getFieldsCount() const
{
    return ResponseCardPart::FIELDS_COUNT;
}

QByteArray DistFileSystem::ResponseCardPart::serialize() const
{
    return Serialization::serialize(serializedParams(), 8);
}

void DistFileSystem::ResponseCardPart::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, 8);
    operator=(l);
}
