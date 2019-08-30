#include "network/packages/service/merged_block_message.h"

using namespace Messages;

MergedBlockMessage::MergedBlockMessage(const Block &first, const Block &second,
                                       const Block &result)
    : BaseMessage(MERGED_BLOCK_MESSAGE)
    , firstBlock(first)
    , secondBlock(second)
    , resultBlock(result)
{
}

MergedBlockMessage::MergedBlockMessage(const QByteArray &serialized)
    : BaseMessage()
{
    QList<QByteArray> msgElemList = BaseMessage::deserializeToList(serialized);
    BaseMessage::initFields(msgElemList);
    initFields(msgElemList);
}

short MergedBlockMessage::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 3;
}

void MergedBlockMessage::initFields(QList<QByteArray> &list)
{
    firstBlock = Block(list.takeFirst());
    secondBlock = Block(list.takeFirst());
    resultBlock = Block(list.takeFirst());
}

QList<QByteArray> MergedBlockMessage::serializedParams() const
{
    QList<QByteArray> l = BaseMessage::serializedParams();
    l << firstBlock.serialize() << secondBlock.serialize() << resultBlock.serialize();
    return l;
}

Block MergedBlockMessage::getFirstBlock() const
{
    return firstBlock;
}

Block MergedBlockMessage::getSecondBlock() const
{
    return secondBlock;
}

Block MergedBlockMessage::getResultBlock() const
{
    return resultBlock;
}
