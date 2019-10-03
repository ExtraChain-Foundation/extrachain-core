#include <headers/network/packages/base_message_response.h>
using namespace Messages;

BaseMessageResponse::BaseMessageResponse(const QByteArray &msg, const QByteArray &hash,
                                         const QByteArray &type)
    : BaseMessage(type)
{
    this->msg_data = msg;
    this->dataHash = hash;
}

BaseMessageResponse::BaseMessageResponse(const BaseMessageResponse &temp)
{
    QList<QByteArray> list = temp.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    dataHash = temp.dataHash;
}

BaseMessageResponse::~BaseMessageResponse()
{
}

BaseMessageResponse BaseMessageResponse::operator=(const BaseMessageResponse &temp)
{
    QList<QByteArray> list = temp.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    dataHash = temp.dataHash;
    return *this;
}

void BaseMessageResponse::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = deserializeToList(serialized);
    this->initFields(list);
}

const QByteArray BaseMessageResponse::hash() const
{
    return dataHash;
}

short BaseMessageResponse::getFieldsCount() const
{
    return this->BaseMessage::getFieldsCount() + FIELDS_COUNT;
}

void BaseMessageResponse::initFields(QList<QByteArray> &list)
{
    BaseMessage::initFields(list);
    dataHash = list.takeFirst();
}

QList<QByteArray> BaseMessageResponse::serializedParams() const
{
    QList<QByteArray> list = BaseMessage::serializedParams();
    list << dataHash;
    return list;
}
