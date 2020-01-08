#ifndef GET_ALL_ACTOR_MESSAGE_H
#define GET_ALL_ACTOR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

struct GetAllActorMessage : IMessage
{
    short FIELD_SIZE = 4;
    short FIELDS_COUNT = 1;
    QList<QByteArray> actorId;

public:
    // IMessage interface
public:
    void operator=(QByteArray &serialized) override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serilaized) override;
};
}
#endif // GET_ALL_ACTOR_MESSAGE_H
