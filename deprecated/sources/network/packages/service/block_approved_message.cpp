#include "network/packages/service/block_approved_message.h"

using namespace Messages;

BlockApprovedMessage::BlockApprovedMessage(const BigNumber &blockId, const BigNumber &approver)
    : BaseMessage(Messages::BLOCK_APPROVED_MESSAGE)
    , blockId(blockId)
    , approver(approver)
{
}

BlockApprovedMessage::BlockApprovedMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

short BlockApprovedMessage::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 2;
}

void BlockApprovedMessage::initFields(QLinkedList<QByteArray> &list)
{
    approver = BigNumber(list.takeLast());
    blockId = BigNumber(list.takeLast());
    BaseMessage::initFields(list);
}

QList<QByteArray> BlockApprovedMessage::serializedParams() const
{
    QList<QByteArray> l = BaseMessage::serializedParams();
    l << blockId.toByteArray() << approver.toActorId();
    return l;
}

const QByteArray BlockApprovedMessage::hash() const
{
    return Utils::calcKeccak(blockId.toByteArray() + approver.toActorId());
}

BigNumber BlockApprovedMessage::getBlockId() const
{
    return blockId;
}

BigNumber BlockApprovedMessage::getApprover() const
{
    return approver;
}
