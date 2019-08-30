#include "network/packages/service/get_tx_message.h"

using namespace Messages;

GetTxMessage::GetTxMessage(const SearchEnum::TxParam param, const QByteArray &value)
    : BaseMessage(GET_TX_MESSAGE)
    , param(param)
    , value(value)
{
}

GetTxMessage::GetTxMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

short GetTxMessage::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 2;
}

void GetTxMessage::initFields(QLinkedList<QByteArray> &list)
{
    value = list.takeLast();
    param = SearchEnum::fromStringTxParam(list.takeLast());
    BaseMessage::initFields(list);
}

QList<QByteArray> GetTxMessage::serializedParams() const
{
    QList<QByteArray> l = BaseMessage::serializedParams();
    l << SearchEnum::toString(param).toLocal8Bit() << value;
    return l;
}

SearchEnum::TxParam GetTxMessage::getParam() const
{
    return param;
}

QByteArray GetTxMessage::getValue() const
{
    return value;
}
