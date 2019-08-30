#ifndef MERGED_BLOCK_MESSAGE_H
#define MERGED_BLOCK_MESSAGE_H

#include "network/packages/base_message.h"
#include "datastorage/block.h"

namespace Messages
{
    static const QByteArray MERGED_BLOCK_MESSAGE = "mergedBlock";

    class MergedBlockMessage : public BaseMessage
    {
    private:
        Block firstBlock;
        Block secondBlock;
        Block resultBlock;

    public:
        MergedBlockMessage(const Block &first, const Block &second,
                           const Block &result);
        MergedBlockMessage(const QByteArray &serialized);

        // BaseMessage interface
    protected:
        short getFieldsCount() const override;
        void initFields(QList<QByteArray> &list) override;
        QList<QByteArray> serializedParams() const override;

    public:
        Block getFirstBlock() const;
        Block getSecondBlock() const;
        Block getResultBlock() const;
    };
}

#endif // MERGED_BLOCK_MESSAGE_H
