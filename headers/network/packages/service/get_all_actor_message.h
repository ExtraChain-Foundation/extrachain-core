#ifndef GET_ALL_ACTOR_MESSAGE_H
#define GET_ALL_ACTOR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

struct GetAllActorMessage : ISmallMessage
{
    static const short FIELD_SIZE = 2;
    static const short FIELDS_COUNT = 1;
    QByteArray actorId;

public:
    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serilaized) override;
};
}
#endif // GET_ALL_ACTOR_MESSAGE_H
