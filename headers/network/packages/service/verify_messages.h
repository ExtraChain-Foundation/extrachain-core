#ifndef VERIFY_MESSAGES_H
#define VERIFY_MESSAGES_H

#include "network/packages/entities/entity_message.h"

namespace Messages {
static const QByteArray VERIFY_ACTOR_MESSAGE = "verifyActor";
static const QByteArray VERIFY_ACTOR_RESPONSE_MESSAGE = "verifyActorResponse";

//    template <class T>
//    class VerifyResponseMessage : public EntityMessage<T>
//    {
//    private:
//        bool verified;

//    public:
//        VerifyResponseMessage(const QByteArray &msgType, const T &data,
//                              bool verified)
//            : EntityMessage<T>(msgType, data)
//            , verified(verified)
//        {
//        }

//        VerifyResponseMessage(const QByteArray &serialized)
//            : EntityMessage<T>(serialized)
//        {
//        }

//    protected:
//        short getFieldsCount() const override
//        {
//            return EntityMessage<T>::getFieldsCount() + 1;
//        }
//        void initFields(QLinkedList<QByteArray> &list) override
//        {
//            verified = list.takeLast().toInt();
//            BaseMessage::initFields(list);
//        }
//        QList<QByteArray> serializedParams() const override
//        {
//            QList<QByteArray> l = BaseMessage::serializedParams();
//            l << QByteArray::number(verified);
//            return l;
//        }

//    public:
//        bool getVerified() const
//        {
//            return verified;
//        }
//    };

//    // Constructing methods //

//    static EntityMessage<Actor<KeyPublic>>
//    createVerifyActorMessage(const Actor<KeyPublic> &actor)
//    {
//        return EntityMessage<Actor<KeyPublic>>(VERIFY_ACTOR_MESSAGE, actor);
//    }

//    static VerifyResponseMessage<Actor<KeyPublic>>
//    createVerifyActorResponseMessage(const Actor<KeyPublic> &actor, bool verified)
//    {
//        return VerifyResponseMessage<Actor<KeyPublic>>(VERIFY_ACTOR_RESPONSE_MESSAGE,
//                                                       actor, verified);
//    }
}

#endif // VERIFY_MESSAGES_H
