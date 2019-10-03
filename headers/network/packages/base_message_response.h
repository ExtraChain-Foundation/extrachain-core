#ifndef BASE_MESSAGE_RESPONSE_H
#define BASE_MESSAGE_RESPONSE_H

#include "headers/network/packages/base_message.h"

namespace Messages {
class BaseMessageResponse : public BaseMessage
{
private:
    QByteArray dataHash;

    static const short FIELDS_COUNT = 1;
    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    BaseMessageResponse(const QByteArray &msg, const QByteArray &hash);
    BaseMessageResponse(const BaseMessageResponse &temp);
    ~BaseMessageResponse() override;
    BaseMessageResponse operator=(const BaseMessageResponse &temp);
    //    QByteArray serialize() const override final;
    void deserialize(const QByteArray &serialized) override;
    const QByteArray hash() const override final;
};
}
#endif // BASE_MESSAGE_RESPONSE_H
