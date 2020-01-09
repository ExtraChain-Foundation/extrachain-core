#ifndef GET_TX_PAIR_MESSAGE_H
#define GET_TX_PAIR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

struct GetTxPairMessage : IMessage
{
    static const short FIELD_SIZE = 2;
    static const short FIELDS_COUNT = 2;

    BigNumber senderId;
    BigNumber receiverId;

    // IMessage interface
public:
    void operator=(QByteArray &serialized) override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serilaized) override;
};
}

#endif // GET_TX_PAIR_MESSAGE_H
