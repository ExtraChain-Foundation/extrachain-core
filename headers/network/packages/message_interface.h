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
const int FIELD_SIZES = 4;
class IMessage : public QObject
{
    Q_OBJECT
protected:
    QByteArray protocol; // protocol version
    static const short FIELDS_COUNT = 1;

public:
    IMessage(QObject *parent = nullptr)
        : QObject(parent)
        , protocol(Config::Net::PROTOCOL_VERSION.toLocal8Bit())
    {
    }
    IMessage(const IMessage &msg, QObject *parent = nullptr)
        : IMessage(parent)
    {
        protocol = msg.protocol;
    }
    ~IMessage()
    {
    }

protected:
    /**
     * @brief Message deserialization (Should be used in constructor)
     * @param serialized message from
     */
    virtual void deserialize(const QByteArray &serialized)
    {
        int count = Utils::qByteArrayToInt(serialized.mid(0, FIELD_SIZES));
        protocol = serialized.mid(4, count);
    }

public:
    /**
     * @brief hash
     * @return
     */
    virtual const QByteArray hash() const = 0;

public:
    /**
     * @brief Message serialization
     * @return serialized message form
     */
    virtual QByteArray serialize() const;
    /**
     * @brief Calculates digital signature from all fields
     * and sets result in digSig field
     * @param key
     */
    virtual void calcDigSig(const Actor<KeyPrivate> &actor) = 0;

    //
    virtual QByteArray serialize(const QList<QByteArray> &list) const = 0;
    /**
     * @brief Verifies digital signatures
     * @param key
     * @return true, if digital signature is valid. False, otherwise.
     */
    virtual bool verifyDigSig(const Actor<KeyPublic> &actor) const = 0;
    /**
     * @brief Get's message signer (actor id)
     * @return signer id
     */
    virtual BigNumber getSigner() const = 0;
    QByteArray getProtocol() const;
};

inline QByteArray IMessage::serialize() const
{

    QByteArray serialized = Utils::intToByteArray(protocol.size(), FIELD_SIZES);
    return serialized + protocol;
}

inline QByteArray IMessage::getProtocol() const
{
    return protocol;
}
}
#endif // IMESSAGE_H
