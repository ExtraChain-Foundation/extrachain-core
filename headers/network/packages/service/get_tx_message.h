#ifndef GET_TX_MESSAGE_H
#define GET_TX_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

struct GetTxMessage : ISmallMessage
{
    static const short FIELD_SIZE = 3;
    static const short FIELDS_COUNT = 2;

    SearchEnum::TxParam param;
    QByteArray value;

    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serilaized) override;
};
}

#endif // GET_TX_MESSAGE_H
