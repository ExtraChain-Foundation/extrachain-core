#include "datastorage/index/blockindex.h"

BlockIndex::BlockIndex()
    : FileIndex(/*DataStorage::BLOCKCHAIN_INDEX + '/' + */ DataStorage::BLOCK_INDEX_FOLDER_NAME)
{
}

BlockIndex::BlockIndex(const BigNumber &recordsLimit)
    : BlockIndex()
{
    this->recordsLimit = recordsLimit;
    qDebug() << "BLOCK INDEX: constructor: recordLimits - " << recordsLimit;
}

BlockIndex::BlockIndex(const QString &folderName)
    : FileIndex(folderName)
{
    qDebug() << "BLOCK INDEX: constructor: folder name - " << folderName;
}

BlockIndex::BlockIndex(const QString &folderName, const BigNumber &recordsLimit)
    : BlockIndex(folderName)
{
    this->recordsLimit = recordsLimit;
}

int BlockIndex::addBlock(const Block &block)
{
    int result = this->add(block.getIndex(), block.serialize());
    return result;
}

Block BlockIndex::getLastBlock() const
{
    BigNumber id = this->lastSavedId;
    qDebug() << "BLOCK INDEX: getLastBlock:"
             << "\n      last saved id - " << this->lastSavedId;
    while (id >= getFirstSavedId())
    {
        Block block = this->getBlockById(id);
        //        qDebug() << "BLOCK - : " << block.serialize();
        if (!block.isEmpty())
        {
            qDebug() << "\n      " << block.getIndex() << " block is not empty";
            return block;
        }
        --id;
    }

    return Block();
}

GenesisBlock BlockIndex::getLastGenesisBlock() const
{
    BigNumber id = this->lastSavedId;
    qDebug() << "BLOCK INDEX: getLastGenesisBlock:"
             << "      last saved id - " << this->lastSavedId;
    while (id >= getFirstSavedId())
    {
        GenesisBlock block = this->getGenesisBlockById(id);
        if (!block.isEmpty())
        {
            qDebug() << "      " << block.getIndex() << " block is empty";
            return block;
        }
        --id;
    }
    return GenesisBlock();
}

GenesisBlock BlockIndex::getGenesisBlockById(const BigNumber &id) const
{
    QByteArray serializedBlock = this->getById(id);
    if (!serializedBlock.isEmpty() && GenesisBlock::isGenesisBlock(serializedBlock))
    {
        return GenesisBlock(serializedBlock);
    }
    return GenesisBlock();
}

Block BlockIndex::getBlockById(const BigNumber &id) const
{
    QByteArray serializedBlock = this->getById(id);
    //    qDebug() << "BLOCK: " << serializedBlock;
    if (!serializedBlock.isEmpty() && Block::isBlock(serializedBlock))
    {
        return Block(serializedBlock);
    }
    else
    {
        qDebug() << "is not block";
    }
    return Block();
}

Block BlockIndex::getBlockByPosition(const BigNumber &position) const
{
    BigNumber blockId = getFirstSavedId() + position;
    if (blockId <= this->lastSavedId)
    {
        Block block = this->getBlockById(blockId);
        return block;
    }
    return Block();
}

Block BlockIndex::getBlockByApprover(const BigNumber &approver) const
{
    return getBlockByParam(approver, SearchEnum::BlockParam::Approver);
}

Block BlockIndex::getBlockByHash(const QByteArray &hash) const
{
    return getBlockByParam(hash, SearchEnum::BlockParam::Hash);
}

Block BlockIndex::getBlockByData(const QByteArray &data) const
{
    return getBlockByParam(data, SearchEnum::BlockParam::Data);
}

Block BlockIndex::getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const
{
    if (param == SearchEnum::BlockParam::Id)
    {
        return getBlockById(id);
    }

    BigNumber lastBlockId = getLastSavedId();

    // iteration from the last to the first Block
    while (lastBlockId >= getFirstSavedId())
    {
        Block lastBlock = getBlockById(lastBlockId);
        switch (param)
        {
        case SearchEnum::BlockParam::Approver:
        {
            if (lastBlock.getApprover() == id)
                return lastBlock;
            break;
        }
        case SearchEnum::BlockParam::Data:
        {
            if (lastBlock.getData() == id)
                return lastBlock;
            break;
        }
        case SearchEnum::BlockParam::Hash:
        {
            if (lastBlock.getHash() == id)
                return lastBlock;
            break;
        }
        default:
            break;
        }
        --lastBlockId;
    }
    return Block();
}

Transaction BlockIndex::getLastTxByHash(const QByteArray &hash, const QByteArray &token) const
{
    return getLastTxByParam(BigNumber(hash), SearchEnum::TxParam::Hash, token);
}

Transaction BlockIndex::getLastTxBySender(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSender, token);
}

Transaction BlockIndex::getLastTxByReceiver(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserReceiver, token);
}

Transaction BlockIndex::getLastTxBySenderOrReceiver(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSenderOrReceiver, token);
}

Transaction BlockIndex::getLastTxBySenderOrReceiverAndToken(const BigNumber &id,
                                                            const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSenderOrReceiverOrToken, token);
}

Transaction BlockIndex::getLastTxByApprover(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserApprover, token);
}

QList<Transaction> BlockIndex::getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from, int count,
                                                             BigNumber token) const
{
    return getTxsByParamInRow(id, SearchEnum::TxParam::UserSenderOrReceiver, from, count, token);
}

// QList<Transaction> BlockIndex::getRecentTxList(const BigNumber &last, const BigNumber &first) const {
//    QList<Transaction> txList;

//}

Transaction BlockIndex::getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param,
                                         const QByteArray &token) const
{
    BigNumber records = getRecords();

    if (records == 0)
    {
        qDebug() << "There no tx's in blockIndex";
        return Transaction();
    }

    BigNumber lastBlockId = getLastSavedId();

    // iterating from last to first block
    while (lastBlockId >= getFirstSavedId())
    {
        Block lastBlock = getBlockById(lastBlockId);
        QList<Transaction> txs = lastBlock.extractTransactions();
        for (const Transaction &tx : txs)
        {
            if (tx.getToken().toActorId() != token)
                continue;
            switch (param)
            {
            case SearchEnum::TxParam::UserSenderOrReceiverOrToken:
            {

                if (tx.getSender() == id || tx.getReceiver() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserSender:
            {
                if (tx.getSender() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserReceiver:
            {
                if (tx.getReceiver() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver:
            {
                if (tx.getSender() == id || tx.getReceiver() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserApprover:
            {
                if (tx.getApprover() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::Hash:
            {
                if (tx.getHash() == id.toActorId())
                    return tx;
                break;
            }
            default:
            {
            }
            }
        }
        --lastBlockId;
    }
    return Transaction();
}

QList<Transaction> BlockIndex::getTxsByParamInRow(const BigNumber &id, SearchEnum::TxParam param,
                                                  BigNumber from, int count, BigNumber token) const
{
    QList<Transaction> currentTxs;
    BigNumber records = getRecords();

    if (records == 0)
    {
        qDebug() << "There no tx's in blockIndex";
        return currentTxs;
    }

    BigNumber lastBlockId = from == -1 ? getLastSavedId() : from;
    int currentCount = 0;

    while (lastBlockId >= getFirstSavedId())
    {
        // qDebug() << count << currentCount << (count < currentCount);

        if (count < currentCount)
            break;

        Block lastBlock = getBlockById(lastBlockId);
        QList<Transaction> txs = lastBlock.extractTransactions();

        for (const Transaction &tx : txs)
        {
            if (tx.getToken() != token)
                continue;
            switch (param)
            {
            case SearchEnum::TxParam::UserSender:
            {
                if (tx.getSender() == id && tx.getToken() == token)
                {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserReceiver:
            {
                if (tx.getReceiver() == id && tx.getToken() == token)
                {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver:
            {
                if ((tx.getSender() == id || tx.getReceiver() == id) && tx.getToken() == token)
                {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserApprover:
            {
                if (tx.getApprover() == id && tx.getToken() == token)
                {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::Hash:
            {
                if (tx.getHash() == id.toActorId() && tx.getToken() == token)
                {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            default:
            {
            }
            }
        }

        --lastBlockId;
    }

    qDebug() << "currentTxs" << currentTxs.length();

    return currentTxs;
}

TxPair BlockIndex::searchPair(const BigNumber &first, const BigNumber &second) const
{
    TxPair pair;

    bool firstFound = false;
    bool secondFound = false;

    BigNumber records = getLastSavedId();
    while (records > 0)
    {
        Block byPosition = getBlockById(records);
        QList<Transaction> trx = byPosition.extractTransactions();

        for (const Transaction &t : trx)
        {
            if (firstFound && secondFound)
            {
                records = 0;
                break;
            }
            if (!firstFound && (t.getSender() == first || t.getReceiver() == first))
            {
                firstFound = true;
                pair.setFirst(t);
            }
            if (!secondFound && (t.getSender() == second || t.getReceiver() == second))
            {
                secondFound = true;
                pair.setSecond(t);
            }
        }
        --records;
    }
    return pair;
}
