#include "network/packages/service/connections_message.h"

void Messages::ConnectionsMessage::operator=(const QByteArray &serialized)
{
    deserialize(serialized);
}

void Messages::ConnectionsMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

void Messages::ConnectionsMessage::operator=(QList<QByteArray> &list)
{
    for (QByteArray d : list)
    {
        std::pair<std::string, int> p;
        QList<QByteArray> r = Serialization::deserialize(d, ConnectionsMessage::FIELD_SIZE);
        p.first = r.takeFirst().toStdString();
        p.second = r.takeFirst().toInt();
        hosts.push_back(p);
    }
}

bool Messages::ConnectionsMessage::isEmpty() const
{
    return hosts.empty();
}

QList<QByteArray> Messages::ConnectionsMessage::serializedParams() const
{
    QList<QByteArray> v;
    for (std::pair<std::string, int> p : hosts)
    {
        QByteArray r = Serialization::serialize(
            { QByteArray::fromStdString(p.first), QByteArray::number(p.second) },
            ConnectionsMessage::FIELD_SIZE);
        v << r;
    }
    return v;
}

short Messages::ConnectionsMessage::getFieldsCount() const
{
    return ConnectionsMessage::FIELDS_COUNT;
}

QByteArray Messages::ConnectionsMessage::serialize() const
{
    QList<QByteArray> list = serializedParams();
    return Serialization::serialize(list, FIELD_SIZE);
}

void Messages::ConnectionsMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> data = Serialization::deserialize(serialized, ConnectionsMessage::FIELD_SIZE);
    operator=(data);
}
