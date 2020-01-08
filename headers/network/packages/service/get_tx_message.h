#ifndef GET_TX_MESSAGE_H
#define GET_TX_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

struct GetTxMessage : IMessage
{
    short FIELD_SIZE = 4;
    short FIELDS_COUNT = 2;

    SearchEnum::TxParam param;
    QByteArray value;

    // IMessage interface
public:
    void operator=(QByteArray &serialized) override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serilaized) override;
};
}

#endif // GET_TX_MESSAGE_H
