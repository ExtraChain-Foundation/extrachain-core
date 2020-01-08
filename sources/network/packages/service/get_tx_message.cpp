#include "network/packages/service/get_tx_message.h"

using namespace Messages;

void Messages::GetTxMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short Messages::GetTxMessage::getFieldsCount() const
{
    return FIELDS_COUNT;
}

QByteArray GetTxMessage::serialize() const
{
    return Serialization::universalSerialize({ SearchEnum::toString(param).toUtf8(), value }, FIELD_SIZE);
}

void GetTxMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDeserialize(serilaized, FIELD_SIZE);
    this->param = SearchEnum::fromStringTxParam(list.at(0));
    this->value = list.at(1);
}
