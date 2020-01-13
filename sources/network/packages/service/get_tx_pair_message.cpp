#include "network/packages/service/get_tx_pair_message.h"

using namespace Messages;

void Messages::GetTxPairMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short Messages::GetTxPairMessage::getFieldsCount() const
{
    return GetTxPairMessage::FIELDS_COUNT;
}

QByteArray GetTxPairMessage::serialize() const
{
    return Serialization::universalSerialize({ senderId.toActorId(), receiverId.toActorId() },
                                             GetTxPairMessage::FIELD_SIZE);
}

void GetTxPairMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDeserialize(serilaized, GetTxPairMessage::FIELD_SIZE);
    this->senderId = BigNumber(list.at(0));
    this->receiverId = BigNumber(list.at(1));
}
