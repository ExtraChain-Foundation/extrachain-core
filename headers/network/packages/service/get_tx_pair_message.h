#ifndef GET_TX_PAIR_MESSAGE_H
#define GET_TX_PAIR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages
{
    static const QByteArray GET_TX_PAIR_MESSAGE = "getTxPair";

    class GetTxPairMessage : public BaseMessage
    {
    private:
        BigNumber senderId;
        BigNumber receiverId;

    public:
        GetTxPairMessage(const BigNumber &senderId, const BigNumber &receiverId);
        GetTxPairMessage(const QByteArray &serialized);

        // BaseMessage interface
    protected:
        short getFieldsCount() const override;
        void initFields(QLinkedList<QByteArray> &list) override;
        QList<QByteArray> serializedParams() const override;

    public:
        BigNumber getSenderId() const;
        BigNumber getReceiverId() const;
    };
}

#endif // GET_TX_PAIR_MESSAGE_H
