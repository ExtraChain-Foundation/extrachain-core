#ifndef GET_BLOCK_MESSAGE_H
#define GET_BLOCK_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

struct GetBlockMessage : IMessage
{
    const short FIELD_SIZE = 4;
    const short FIELDS_COUNT = 2;

    SearchEnum::BlockParam param;
    QByteArray value;

    // IMessage interface
public:
    void operator=(QByteArray &serialized) override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serilaized) override;
};
}

#endif // GET_BLOCK_MESSAGE_H
