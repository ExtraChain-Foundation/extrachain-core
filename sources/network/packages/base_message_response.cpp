#include <headers/network/packages/base_message_response.h>
using namespace Messages;

void BaseMessageResponse::operator=(BaseMessageResponse bm)
{
    QList<QByteArray> list = bm.BaseMessage::serializedParams();
    BaseMessage::operator=(list);
    dataHash = bm.dataHash;
}
// IMessage interface
void BaseMessageResponse::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

void BaseMessageResponse::operator=(QList<QByteArray> &list)
{
    BaseMessage::operator=(list);
    dataHash = list.takeFirst();
}

bool BaseMessageResponse::isEmpty() const
{
    if (BaseMessage::isEmpty() || dataHash.isEmpty())
        return true;
    else
        return false;
}

QList<QByteArray> BaseMessageResponse::serializedParams() const
{
    QList<QByteArray> list = BaseMessage::serializedParams();
    list << dataHash;
    return list;
}

short BaseMessageResponse::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + FIELDS_COUNT;
}
