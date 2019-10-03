#ifndef GET_TX_PAIR_MESSAGE_H
#define GET_TX_PAIR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {
static const QByteArray GET_TX_PAIR_MESSAGE = "getTxPair";

class GetTxPairMessage
{
    const short FIELDS_SIZE = 4;

private:
    BigNumber senderId;
    BigNumber receiverId;

public:
    GetTxPairMessage(const BigNumber &senderId, const BigNumber &receiverId);
    GetTxPairMessage(const QByteArray &serialized);

    // BaseMessage interface
    ~GetTxPairMessage();

    const QByteArray serialize() const;
    void deserialize(const QByteArray &serilaized);

    BigNumber getSenderId() const;
    BigNumber getReceiverId() const;
};
}

#endif // GET_TX_PAIR_MESSAGE_H
