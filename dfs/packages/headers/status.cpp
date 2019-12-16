#include "status.h"

DFSMessage::Status::Status(const QByteArray &serialize)
    : DUMessage(type_status)
{
    QByteArrayList list = deserialize(serialize);
    if (type_status != list.takeFirst().toInt())
    {
        qDebug() << "[status]"
                 << "incorrect message type";
    }
    if (list.size() != FIELDS_COUNT)
    {
        qDebug() << "title_message_struct << incorrect input data";
        return;
    }
    hash = list.takeFirst();
    dirOwner = list.takeFirst();
    currentState = deserializeState(list.takeFirst());
}

DFSMessage::Status::Status(const QByteArray &dirOwner, const QStringList &state)
    : DUMessage(type_status)
{
    this->dirOwner = dirOwner;
    currentState = state;
    hash = Utils::calcKeccak(serializeState());
}

DFSMessage::Status::~Status()
{
}

const QStringList DFSMessage::Status::deserializeState(const QByteArray &serialized)
{
    QList<QByteArray> list = Serialization::deserialize(serialized, stateDelimetr);
    QStringList result;
    for (const QByteArray &el : list)

        result << el;
    return result;
}

const QByteArray DFSMessage::Status::serializeState() const
{
    QList<QByteArray> list;
    for (const QString &el : currentState)
        list << el.toUtf8();
    return Serialization::serialize(list, stateDelimetr);
}

const QList<QByteArray> DFSMessage::Status::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << hash << dirOwner << serializeState();
    return list;
}
