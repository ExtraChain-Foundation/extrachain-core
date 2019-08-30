#include "network/packages/service/get_tx_pair_message.h"

using namespace Messages;

GetTxPairMessage::GetTxPairMessage(const BigNumber &senderId,
                                   const BigNumber &receiverId)
    : BaseMessage(GET_TX_PAIR_MESSAGE)
    , senderId(senderId)
    , receiverId(receiverId)
{
}

GetTxPairMessage::GetTxPairMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

short GetTxPairMessage::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 2;
}

void GetTxPairMessage::initFields(QLinkedList<QByteArray> &list)
{
    receiverId = BigNumber(list.takeLast());
    senderId = BigNumber(list.takeLast());
    BaseMessage::initFields(list);
}

QList<QByteArray> GetTxPairMessage::serializedParams() const
{
    QList<QByteArray> l = BaseMessage::serializedParams();
    l << senderId.serialize() << receiverId.serialize();
    return l;
}

BigNumber GetTxPairMessage::getSenderId() const
{
    return senderId;
}

BigNumber GetTxPairMessage::getReceiverId() const
{
    return receiverId;
}
