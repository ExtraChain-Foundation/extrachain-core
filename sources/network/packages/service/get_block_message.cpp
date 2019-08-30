#include "network/packages/service/get_block_message.h"

using namespace Messages;

GetBlockMessage::GetBlockMessage(const SearchEnum::BlockParam param, const QByteArray &value)
    : BaseMessage(GET_BLOCK_MESSAGE)
    , param(param)
    , value(value)
{
}

GetBlockMessage::GetBlockMessage(const QByteArray &serialized)
{
    QList<QByteArray> msgElemList = BaseMessage::deserializeToList(serialized);
//    BaseMessage::initFields(msgElemList);
    initFields(msgElemList);
}

short GetBlockMessage::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 2;
}

void GetBlockMessage::initFields(QList<QByteArray> &list)
{
    value = list.takeLast();
    param = SearchEnum::fromStringBlockParam(list.takeLast());
    BaseMessage::initFields(list);
}

QList<QByteArray> GetBlockMessage::serializedParams() const
{
    QList<QByteArray> l = BaseMessage::serializedParams();
    l << SearchEnum::toString(param).toLocal8Bit() << value;
    return l;
}

SearchEnum::BlockParam GetBlockMessage::getParam() const
{
    return param;
}

QByteArray GetBlockMessage::getValue() const
{
    return value;
}
