#ifndef CHAT_MESSAGE_H
#define CHAT_MESSAGE_H

#include "network/packages/base_message.h"
#include "utils/utils.h"

namespace Messages {
static const QByteArray INVITE_CHAT_MESSAGE = "chatInvateMessage";
static const QByteArray CHAT_MESSAGE = "chatMessage";

class ChatMessage : public BaseMessage
{
private:
    QByteArray message;
    BigNumber recipient;

public:
    ChatMessage(const BigNumber &recipientId, const QByteArray message)
        : BaseMessage(CHAT_MESSAGE)
        , message(message)
        , recipient(recipientId)
    {
    }

    ChatMessage(const QByteArray &message);

    // BaseMessage interface
protected: // maybe protected
    short getFieldsCount() const override;
    void initFields(QLinkedList<QByteArray> &list) override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    QByteArray getMessage() const;
    //    QByteArray  serialize() const override;
};
}

#endif // CHAT_MESSAGE_H
