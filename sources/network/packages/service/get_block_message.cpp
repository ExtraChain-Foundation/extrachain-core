#include "network/packages/service/get_block_message.h"

using namespace Messages;

GetBlockMessage::GetBlockMessage(const SearchEnum::BlockParam param, const QByteArray &value)
{
    this->param = param;
    this->value = value;
}

GetBlockMessage::GetBlockMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

GetBlockMessage::~GetBlockMessage()
{
}

const QByteArray GetBlockMessage::serialize() const
{
    return Serialization::universalSerialize({ SearchEnum::toString(param).toUtf8(), value }, FIELDS_SIZE);
}

void GetBlockMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDesirialize(serilaized, FIELDS_SIZE);
    this->param = SearchEnum::fromStringBlockParam(list.at(0));
    this->value = list.at(1);
}

SearchEnum::BlockParam GetBlockMessage::getParam() const
{
    return param;
}

QByteArray GetBlockMessage::getValue() const
{
    return value;
}
