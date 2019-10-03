#ifndef BASE_MESSAGE_RESPONSE_H
#define BASE_MESSAGE_RESPONSE_H

#include "headers/network/packages/base_message.h"

namespace Messages {
static const QByteArray GET_RESERVE_ACTOR_RESPONSE_MESSAGE = "getReserveActorResponse";
static const QByteArray GET_BLOCK_COUNT_RESPONSE_MESSAGE = "getBlockCountResponse";
static const QByteArray GET_ACTOR_COUNT_RESPONSE_MESSAGE = "getActorCountResponse";

static const QByteArray GET_BLOCK_RESPONSE_MESSAGE = "getBlockResponse";
static const QByteArray GET_ACTOR_RESPONSE_MESSAGE = "getActorResponse";
static const QByteArray GET_TX_RESPONSE_MESSAGE = "getTxResponse";
static const QByteArray GET_TX_PAIR_RESPONSE_MESSAGE = "getTxPairResponse";
class BaseMessageResponse : public BaseMessage
{
private:
    QByteArray dataHash;

    static const short FIELDS_COUNT = 1;
    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    BaseMessageResponse(const QByteArray &msg, const QByteArray &hash, const QByteArray &type);
    BaseMessageResponse(const BaseMessageResponse &temp);
    ~BaseMessageResponse() override;
    BaseMessageResponse operator=(const BaseMessageResponse &temp);
    //    QByteArray serialize() const override final;
    void deserialize(const QByteArray &serialized) override;
    const QByteArray hash() const override final;
};
}
#endif // BASE_MESSAGE_RESPONSE_H
