#ifndef BASE_MESSAGE_RESPONSE_H
#define BASE_MESSAGE_RESPONSE_H

#include "network/packages/base_message.h"

namespace Messages {
struct BaseMessageResponse : BaseMessage
{
    QByteArray dataHash;

    static const short FIELDS_COUNT = 1;
    void operator=(BaseMessageResponse bm);
    // IMessage interface
public:
    virtual void operator=(QByteArray &serialized) override;
    virtual void operator=(QList<QByteArray> &list) override;
    virtual bool isEmpty() const override;
    //    virtual QByteArray concatenateAllData() const override;
    virtual QList<QByteArray> serializedParams() const override;
    virtual short getFieldsCount() const override;
    //    virtual QByteArray serialize() const override;
    //    virtual void deserialize(const QByteArray &serialized) override;
    //    virtual const QByteArray hash() const override;

    // BaseMessage interface
    // public:
    //    virtual void calcDigSig(const Actor<KeyPrivate> &actor) override;
    //    virtual bool verifyDigSig(const Actor<KeyPublic> &actor) const override;
};
}
#endif // BASE_MESSAGE_RESPONSE_H
