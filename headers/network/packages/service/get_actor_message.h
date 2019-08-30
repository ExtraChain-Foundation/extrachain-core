#ifndef GET_ACTOR_MESSAGE_H
#define GET_ACTOR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {
static const QByteArray GET_ACTOR_MESSAGE = "getActors";

class GetActorMessage : public BaseMessage
{
private:
    BigNumber actorId;

public:
    GetActorMessage(const BigNumber &actorId);
    GetActorMessage(const QByteArray &serialized);

    // BaseMessage interface
protected:
    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    BigNumber getActorId() const;
};
}

#endif // GET_ACTOR_MESSAGE_H
