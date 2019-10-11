#include "network/packages/service/get_tx_pair_message.h"

using namespace Messages;

GetTxPairMessage::GetTxPairMessage(const BigNumber &senderId, const BigNumber &receiverId)
{
    this->senderId = senderId;
    this->receiverId = receiverId;
}

GetTxPairMessage::GetTxPairMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

GetTxPairMessage::~GetTxPairMessage()
{
}

const QByteArray GetTxPairMessage::serialize() const
{
    return Serialization::universalSerialize({ senderId.toActorId(), receiverId.toActorId() }, FIELDS_SIZE);
}

void GetTxPairMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDesirialize(serilaized, FIELDS_SIZE);
    this->senderId = BigNumber(list.at(0));
    this->receiverId = BigNumber(list.at(1));
}

BigNumber GetTxPairMessage::getSenderId() const
{
    return senderId;
}

BigNumber GetTxPairMessage::getReceiverId() const
{
    return receiverId;
}
