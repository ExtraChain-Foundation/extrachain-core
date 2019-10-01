#ifndef BASEMESSAGE_H
#define BASEMESSAGE_H

#include <QLinkedList>

#include "network/packages/message_interface.h"
#include "utils/bignumber.h"
#include "utils/utils.h"

namespace Messages {
/**
 * @brief Base package class
 * Simple implementation of IMessage interface
 *
 * Methods that should be overrided in subclasses:
 * 1) getFieldsCount
 * 2) serializedParams
 * 3) initFields
 *
 */
class BaseMessage : public IMessage
{
private:
    //    QByteArray protocol; // protocol version
    QByteArray msgType; // message type
    BigNumber signer;   // message signer actor's id
protected:
    QByteArray digSig; // for security

public:
    static const short FIELDS_COUNT = 3; // 4;

public:
    BaseMessage();
    BaseMessage(const BaseMessage &msg);
    BaseMessage(const QByteArray &msgType);
    ~BaseMessage() override;

protected:
    /**
     * @brief Concatenates all data (used in digSig calculation)
     * @return all payload without delimiters
     */
    virtual QByteArray concatenateAllData() const;

protected:
    /**
     * @brief Extract state. USE THIS METHOD IN SUBCLASSES.
     * @param list with serialized fields on 0 to FIELDS_COUNT positions
     */
    virtual void initFields(QLinkedList<QByteArray> &list);
    virtual void initFields(QList<QByteArray> &list);

    // Override this methods and add subclasses fields.
protected:
    virtual short getFieldsCount() const;

    /**
     * @brief Collect's all fields in serialized form
     * @return qlist
     */
    virtual QList<QByteArray> serializedParams() const;

public:
    void deserialize(const QByteArray &serialized) override;
    QList<QByteArray> deserializeToList(const QByteArray &serialized);

    // IMessage interface
public:
    QByteArray serialize() const override;
    QByteArray serialize(const QList<QByteArray> &list) const override final;

    void calcDigSig(const Actor<KeyPrivate> &actor) override final;
    bool verifyDigSig(const Actor<KeyPublic> &actor) const override final;

    /**
     * @brief Deserialization
     * @param serialized msg
     * @return messages
     */
    static BaseMessage deserializeMsg(const QByteArray serialized);

    const QByteArray hash() const override;
    const QByteArray init(const QByteArray &data) const;

public:
    QByteArray getProtocol() const;
    QByteArray getMsgType() const;
    BigNumber getSigner() const override;
    QByteArray getDigSig() const;
};
class BaseMessageResponse : public BaseMessage
{
private:
    QByteArray dataHash;
    static const short FIELDS_COUNT = 1;
    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    BaseMessageResponse(const QByteArray &msg);
    BaseMessageResponse(const BaseMessage &msg, const QByteArray &hash);

    ~BaseMessageResponse() override;
    BaseMessageResponse operator=(const BaseMessageResponse &temp);
    QByteArray serialize() const override final;
    void deserialize(const QByteArray &serialized) override;
    const QByteArray hash() const override final;
};
}
#endif // BASEMESSAGE_H
