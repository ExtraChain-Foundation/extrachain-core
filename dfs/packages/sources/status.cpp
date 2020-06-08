#include "dfs/packages/headers/status.h"

const QStringList DistFileSystem::Status::deserializeState(const QByteArray &serialized)
{
    // QList<QByteArray> list = Serialization::deserialize(serialized, stateDelimetr);
    QStringList result;
    // for (const QByteArray &el : list)
    //    result << el;
    return result;
}

const QByteArray DistFileSystem::Status::serializeState() const
{
    // QList<QByteArray> list;
    // for (const QString &el : currentState)
    //    list << el.toUtf8();
    return ""; // Serialization::serialize(list, stateDelimetr);
}

const QList<QByteArray> DistFileSystem::Status::serializedParams() const
{
    QList<QByteArray> list;
    list << hash << dirOwner << serializeState();
    return list;
}

void DistFileSystem::Status::calcHash()
{
    QList<QByteArray> lt;
    for (QString s : currentState)
    {
        lt.append(s.toUtf8());
    }
    hash = Utils::calcKeccak(Serialization::serialize(lt));
}

void DistFileSystem::Status::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::Status::isEmpty() const
{
    return hash.isEmpty() || dirOwner.isEmpty() || currentState.isEmpty();
}

short DistFileSystem::Status::getFieldsCount() const
{
    return Status::FIELDS_COUNT;
}

QByteArray DistFileSystem::Status::serialize() const
{
    return Serialization::serialize(serializedParams(), DistFileSystem::fieldsSize);
}

void DistFileSystem::Status::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, DistFileSystem::fieldsSize);
    hash = l.takeFirst();
    dirOwner = l.takeFirst();
    currentState = deserializeState(l.takeFirst());
}
