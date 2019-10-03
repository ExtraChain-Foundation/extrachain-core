#ifndef GET_TX_MESSAGE_H
#define GET_TX_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {
static const QByteArray GET_TX_MESSAGE = "getTx";

class GetTxMessage
{
    short FIELDS_SIZE = 4;

private:
    SearchEnum::TxParam param;
    QByteArray value;

public:
    GetTxMessage(const SearchEnum::TxParam param, const QByteArray &value);
    GetTxMessage(const QByteArray &serialized);
    ~GetTxMessage();
    const QByteArray serialize() const;
    void deserialize(const QByteArray &serilaized);
    SearchEnum::TxParam getParam() const;
    QByteArray getValue() const;
};
}

#endif // GET_TX_MESSAGE_H
