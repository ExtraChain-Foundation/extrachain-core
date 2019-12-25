#ifndef GET_BLOCK_MESSAGE_H
#define GET_BLOCK_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

class GetBlockMessage
{
    const short FIELDS_SIZE = 4;

private:
    SearchEnum::BlockParam param;
    QByteArray value;

public:
    GetBlockMessage(const SearchEnum::BlockParam param, const QByteArray &value);
    GetBlockMessage(const QByteArray &serialized);
    ~GetBlockMessage();

    const QByteArray serialize() const;
    void deserialize(const QByteArray &serilaized);

    SearchEnum::BlockParam getParam() const;
    QByteArray getValue() const;
};
}

#endif // GET_BLOCK_MESSAGE_H
