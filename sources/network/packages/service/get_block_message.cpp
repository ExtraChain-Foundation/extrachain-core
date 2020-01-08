#include "network/packages/service/get_block_message.h"

using namespace Messages;

void GetBlockMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short GetBlockMessage::getFieldsCount() const
{
    return GetBlockMessage::FIELDS_COUNT;
}

QByteArray GetBlockMessage::serialize() const
{
    return Serialization::universalSerialize({ SearchEnum::toString(param).toUtf8(), value }, FIELD_SIZE);
}

void GetBlockMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDeserialize(serilaized, FIELD_SIZE);
    this->param = SearchEnum::fromStringBlockParam(list.at(0));
    this->value = list.at(1);
}
