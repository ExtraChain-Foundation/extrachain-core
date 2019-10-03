#include "network/packages/service/get_tx_message.h"

using namespace Messages;

GetTxMessage::GetTxMessage(const SearchEnum::TxParam param, const QByteArray &value)
{
    this->param = param;
    this->value = value;
}

GetTxMessage::GetTxMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

GetTxMessage::~GetTxMessage()
{
}

const QByteArray GetTxMessage::serialize() const
{
    return Serialization::universalSerialize({ SearchEnum::toString(param).toUtf8(), value }, FIELDS_SIZE);
}

void GetTxMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDesirialize(serilaized, FIELDS_SIZE);
    this->param = SearchEnum::fromStringTxParam(list.at(0));
    this->value = list.at(1);
}

SearchEnum::TxParam GetTxMessage::getParam() const
{
    return param;
}

QByteArray GetTxMessage::getValue() const
{
    return value;
}
