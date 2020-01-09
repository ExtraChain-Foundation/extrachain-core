#ifndef IMESSAGE_H
#define IMESSAGE_H

#include <QByteArray>

#include "datastorage/actor.h"
#include "enc/key_private.h"
#include "enc/key_public.h"

namespace Messages {
/**
 * @brief The IMessage interface
 * Every new message should implement this interface
 */
const int FIELD_SIZE = 8;
struct ISmallMessage
{
    virtual void operator=(QByteArray &serialized) = 0;

    virtual bool isEmpty() const = 0;

    virtual short getFieldsCount() const = 0;
    /**
     * @brief Message serialization
     * @return serialized message form
     */
    virtual QByteArray serialize() const = 0;

    /**
     * @brief Message deserialization (Should be used in constructor)
     * @param serialized message from
     */
    virtual void deserialize(const QByteArray &serialized) = 0;
};
struct IMessage : ISmallMessage
{
    virtual void operator=(QByteArray &serialized) = 0;

    virtual void operator=(QList<QByteArray> &list) = 0;

    virtual bool isEmpty() const = 0;

    virtual QByteArray concatenateAllData() const = 0;

    virtual QList<QByteArray> serializedParams() const = 0;

    virtual short getFieldsCount() const = 0;
    /**
     * @brief Message serialization
     * @return serialized message form
     */
    virtual QByteArray serialize() const = 0;

    /**
     * @brief Message deserialization (Should be used in constructor)
     * @param serialized message from
     */
    virtual void deserialize(const QByteArray &serialized) = 0;
    //    /**
    //     * @brief Calculates digital signature from all fields
    //     * and sets result in digSig field
    //     * @param key
    //     */
    //    virtual void calcDigSig(const Actor<KeyPrivate> &actor) = 0;
    //    /**
    //     * @brief Verifies digital signatures
    //     * @param key
    //     * @return true, if digital signature is valid. False, otherwise.
    //     */
    //    virtual bool verifyDigSig(const Actor<KeyPublic> &actor) const = 0;
    /**
     * @brief hash
     * @return
     */
    virtual const QByteArray hash() const = 0;
};
}
#endif // IMESSAGE_H
