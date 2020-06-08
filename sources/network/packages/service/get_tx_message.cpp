#include "network/packages/service/get_tx_message.h"

using namespace Messages;

void Messages::GetTxMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool Messages::GetTxMessage::isEmpty() const
{
    return (param == SearchEnum::TxParam::Null) || (value.isEmpty());
}

short Messages::GetTxMessage::getFieldsCount() const
{
    return FIELDS_COUNT;
}

QByteArray GetTxMessage::serialize() const
{
    return Serialization::serialize({ SearchEnum::toString(param).toUtf8(), value },
                                             GetTxMessage::FIELD_SIZE);
}

void GetTxMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::deserialize(serilaized, GetTxMessage::FIELD_SIZE);
    this->param = SearchEnum::fromStringTxParam(list.at(0));
    this->value = list.at(1);
}
