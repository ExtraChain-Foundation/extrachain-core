#ifndef GET_TX_MESSAGE_H
#define GET_TX_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages
{
    static const QByteArray GET_TX_MESSAGE = "getTx";

    class GetTxMessage : public BaseMessage
    {
    private:
        SearchEnum::TxParam param;
        QByteArray value;

    public:
        GetTxMessage(const SearchEnum::TxParam param, const QByteArray &value);
        GetTxMessage(const QByteArray &serialized);

        // BaseMessage interface
    protected:
        short getFieldsCount() const override;
        void initFields(QLinkedList<QByteArray> &list) override;
        QList<QByteArray> serializedParams() const override;

    public:
        SearchEnum::TxParam getParam() const;
        QByteArray getValue() const;
    };
}

#endif // GET_TX_MESSAGE_H
