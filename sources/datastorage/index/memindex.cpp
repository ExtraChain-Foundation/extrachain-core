#include "datastorage/index/memindex.h"

MemIndex::MemIndex()
{
    //
}

MemIndex::~MemIndex()
{
    //
}

int MemIndex::addBlock(const Block &block)
{
    if (blocks.contains(block))
    {
        qDebug() << "Block [" << block.toString() << "] already exists";
        return 1;
    }

    if (Config::DataStorage::MEM_INDEX_SIZE_LIMIT <= blocks.size())
    {
        qWarning() << "MemBlock is filled";
        // todo: enable FILE mode
        return 2;
    }
    blocks.append(block);
    return 0;
}

int MemIndex::removeById(const BigNumber &blockId)
{
    const int sizeWas = blocks.size();
    blocks.erase(remove_if(begin(blocks), end(blocks),
                           [&](const Block &block) { return block.getIndex() == blockId; }));
    if (sizeWas == blocks.size())
    {
        qDebug() << "There no record with id:" << blockId;
        return 1;
    }
    return 0;
}

int MemIndex::getRecords() const
{
    return blocks.size();
}

bool MemIndex::contains(const BigNumber &blockId) const
{
    Block block = this->operator[](blockId);
    return !block.isEmpty();
}

Block MemIndex::operator[](const BigNumber &blockId) const
{
    if (contains(blockId))
    {
        QList<Block>::const_iterator it = find_if(
            begin(blocks), end(blocks), [&](const Block &block) { return block.getIndex() == blockId; });
        if (it != end(blocks))
        {
            return *it;
        }
    }
    qDebug() << "There no record with id:" << blockId;
    return Block();
}

Block MemIndex::getByPosition(int pos) const
{
    return blocks.at(pos);
}

Block MemIndex::getLastBlock() const
{
    if (blocks.size() > 0)
    {
        return blocks.at(blocks.size() - 1);
    }
    return Block();
}

Block MemIndex::getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const
{
    int index = getRecords() - 1;

    // iteration from the last to the first Block
    while (index >= 0)
    {
        Block byPosition = getByPosition(index);
        switch (param)
        {
        case SearchEnum::BlockParam::Approver:
        {
            if (byPosition.getApprover() == id)
                return byPosition;
            break;
        }
        case SearchEnum::BlockParam::Data:
        {
            if (byPosition.getData() == id)
                return byPosition;
            break;
        }
        case SearchEnum::BlockParam::Hash:
        {
            if (byPosition.getHash() == id)
                return byPosition;
            break;
        }
        case SearchEnum::BlockParam::Id:
        {
            if (byPosition.getIndex() == id)
                return byPosition;
            break;
        }
        default:
            break;
        }
        --index;
    }
    return Block();
}

Block MemIndex::getByApprover(const BigNumber &approver) const
{
    return getBlockByParam(approver, SearchEnum::BlockParam::Approver);
}

Block MemIndex::getByData(const QByteArray &data) const
{
    return getBlockByParam(data, SearchEnum::BlockParam::Data);
}

Block MemIndex::getByHash(const QByteArray &hash) const
{
    return getBlockByParam(hash, SearchEnum::BlockParam::Hash);
}

Transaction MemIndex::getLastTxByHash(const QByteArray &hash, const QByteArray &token) const
{
    return getLastTxByParam(BigNumber(hash), SearchEnum::TxParam::Hash, token);
}

Transaction MemIndex::getLastTxBySender(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSender, token);
}

Transaction MemIndex::getLastTxByReceiver(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserReceiver, token);
}

Transaction MemIndex::getLastTxBySenderOrReceiver(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSenderOrReceiver, token);
}

Transaction MemIndex::getLastTxBySenderOrReceiverAndToken(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSenderOrReceiverOrToken, token);
}

Transaction MemIndex::getLastTxByApprover(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserApprover, token);
}

void MemIndex::removeAll()
{
    this->blocks.clear();
}

Transaction MemIndex::getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param,
                                       const QByteArray &token) const
{
    int records = getRecords();

    if (records == 0)
    {
        qDebug() << "There no tx's in memIndex";
        return Transaction();
    }

    int lastIndex = records - 1;

    // iterating from last to first block
    while (lastIndex >= 0)
    {
        Block byPosition = getByPosition(lastIndex);
        QList<Transaction> txs = byPosition.extractTransactions();
        for (const Transaction &tx : txs)
        {
            if (tx.getToken().toActorId() != token)
                continue;
            switch (param)
            {
            case SearchEnum::TxParam::UserSenderOrReceiverOrToken:
            {
                QList<QByteArray> data =
                    Serialization::deserialize(id.toActorId(), Serialization::TX_FIELD_SPLITTER);
                if (data.size() != 2)
                {
                    qDebug() << "[memindex.cpp][getLastTxByParam] Error when get Search parameter "
                                "UserSenderOrReceiverOrToken. List size !=2";
                    qInfo() << "[memindex.cpp][getLastTxByParam] Error when get Search parameter "
                               "UserSenderOrReceiverOrToken. List size !=2";
                    return Transaction();
                }

                BigNumber token = data[1];              // get value from desearizing id.
                BigNumber idSenderOrReceiver = data[0]; // get value form desearizing id.
                if ((tx.getSender() == idSenderOrReceiver && tx.getToken() == token)
                    || (tx.getReceiver() == idSenderOrReceiver && tx.getToken() == token))
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
        --lastIndex;
    }
    return Transaction();
}

TxPair MemIndex::searchPair(const BigNumber &first, const BigNumber &second) const
{
    TxPair pair;

    bool firstFound = false;
    bool secondFound = false;

    int records = getRecords();
    int index = records - 1;
    while (index >= 0)
    {
        Block byPosition = getByPosition(index);
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
        --index;
    }
    return pair;
}
