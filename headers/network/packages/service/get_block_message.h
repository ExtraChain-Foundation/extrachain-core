#ifndef GET_BLOCK_MESSAGE_H
#define GET_BLOCK_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages
{
    static const QByteArray GET_BLOCK_MESSAGE = "getBlock";

    class GetBlockMessage : public BaseMessage
    {
    private:
        SearchEnum::BlockParam param;
        QByteArray value;

    public:
        GetBlockMessage(const SearchEnum::BlockParam param, const QByteArray &value);
        GetBlockMessage(const QByteArray &serialized);

        // BaseMessage interface
    protected:
        short getFieldsCount() const override;
        void initFields(QList<QByteArray> &list) override;
        QList<QByteArray> serializedParams() const override;

    public:
        SearchEnum::BlockParam getParam() const;
        QByteArray getValue() const;
    };
}

#endif // GET_BLOCK_MESSAGE_H
