#include "network/packages/service/list_connections.h"

QList<std::pair<int, std::string>> Messages::EnableConnections::getEnableConnections() const
{
    return enableConnections;
}

Messages::EnableConnections Messages::EnableConnections::
operator=(const Messages::EnableConnections &value)
{
    enableConnections = value.enableConnections;
    return *this;
}

QList<QByteArray> Messages::EnableConnections::pairSerialize() const
{
    QList<QByteArray> allConnections;
    for (auto &el : enableConnections)
    {
        QList<QByteArray> l;
        l << QByteArray::number(el.first) << QByteArray::fromStdString(el.second);
        allConnections << Serialization::serialize(l, Serialization::TX_PAIR_FIELD_SPLITTER);
    }
    return allConnections;
}

void Messages::EnableConnections::pairDesirialize(const QByteArray &serialized)
{
    QList<QByteArray> allConnections =
        Serialization::deserialize(serialized, Serialization::DEFAULT_LIST_SPLITTER);
    for (QByteArray &el : allConnections)
    {
        QList<QByteArray> l =
            Serialization::deserialize(el, Serialization::TX_PAIR_FIELD_SPLITTER);
        enableConnections << std::make_pair(l.takeFirst().toInt(),
                                            l.takeFirst().toStdString());
    }
}

short Messages::EnableConnections::getFieldsCount() const
{
    return this->BaseMessage::getFieldsCount() + FIELDS_COUNT;
}

void Messages::EnableConnections::initFields(QList<QByteArray> &list)
{
    this->BaseMessage::initFields(list);
    pairDesirialize(list.takeFirst());
}

QList<QByteArray> Messages::EnableConnections::serializedParams() const
{
    QList<QByteArray> list = this->BaseMessage::serializedParams();

    list << Serialization::serialize(pairSerialize(), Serialization::DEFAULT_LIST_SPLITTER);
    return list;
}

Messages::EnableConnections::EnableConnections(const QByteArray &serialize)
    : Messages::BaseMessage(ENABLE_LIST_CONNECTIONS)
{
    deserialize(serialize);
}

Messages::EnableConnections::EnableConnections(const QList<std::pair<int, std::string>> &list)
    : BaseMessage(ENABLE_LIST_CONNECTIONS)
{
    enableConnections = list;
}

Messages::EnableConnections::EnableConnections(const Messages::EnableConnections &connections)
{
    enableConnections = connections.enableConnections;
}

Messages::EnableConnections::~EnableConnections()
{
}

QByteArray Messages::EnableConnections::serialize() const
{
    QList<QByteArray> list = serializedParams();
    return Serialization::universalSerialize(list, FIELD_SIZES);
}

void Messages::EnableConnections::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = BaseMessage::deserializeToList(serialized);
    this->BaseMessage::initFields(list);
    initFields(list);
}
