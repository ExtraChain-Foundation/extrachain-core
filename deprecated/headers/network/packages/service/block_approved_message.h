#ifndef BLOCK_APPROVED_MESSAGE_H
#define BLOCK_APPROVED_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {
static const QByteArray BLOCK_APPROVED_MESSAGE = "blockApproved";

class BlockApprovedMessage : public BaseMessage
{
private:
    BigNumber blockId;
    BigNumber approver;

public:
    BlockApprovedMessage(const BigNumber &blockId, const BigNumber &approver);
    BlockApprovedMessage(const QByteArray &serialized);

    // IMessage interface
protected:
    short getFieldsCount() const override;
    void initFields(QLinkedList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;
    const QByteArray hash() const override final;

public:
    BigNumber getBlockId() const;
    BigNumber getApprover() const;
};
}

#endif // BLOCK_APPROVED_MESSAGE_H
