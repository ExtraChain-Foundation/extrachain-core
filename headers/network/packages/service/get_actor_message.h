#ifndef GET_ACTOR_MESSAGE_H
#define GET_ACTOR_MESSAGE_H

#include "network/packages/message_interface.h"

namespace Messages {

struct GetActorMessage : IMessage
{
    static const short FIELDS_COUNT = 1;
    static const short FIELD_SIZE = 2;

public:
    BigNumber actorId;

public:
    //    const QByteArray serialize() const;
    //    void deserialize(const QByteArray &serilaized);

    // IMessage interface
public:
    void operator=(QByteArray &serialized) override;
    //    virtual void operator=(QList<QByteArray> &list) override;
    //    virtual bool isEmpty() override;
    //    virtual QByteArray concatenateAllData() const override;
    //    virtual QList<QByteArray> serializedParams() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
    //    virtual const QByteArray hash() const override;
};
}

#endif // GET_ACTOR_MESSAGE_H
