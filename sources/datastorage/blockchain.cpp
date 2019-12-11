#include "datastorage/blockchain.h"

Blockchain::Blockchain(AccountController *accountController, bool fileMode)
    : fileMode(fileMode)
    , accountController(accountController)
{
    actorIndex = accountController->getActorIndex();
    genBlockData.clear();
}

Blockchain::~Blockchain()
{
}

BigNumber Blockchain::checkIntegrity()
{
    if (fileMode)
    {
        // check in MemIndex: start from second block
        for (int i = 1; i < memIndex.getRecords(); i++)
        {
            Block prev = memIndex.getByPosition(i - 1);
            Block cur = memIndex.getByPosition(i);
            if (cur.getPrevHash() != prev.getHash())
            {
                return cur.getIndex();
            }
        }
    }
    else
    {
        // check in FileIndex: start from second block
        for (BigNumber i = 1; i < blockIndex.getRecords(); i++)
        {
            Block prev = blockIndex.getBlockByPosition(i - 1);
            Block cur = blockIndex.getBlockByPosition(i);
            if (cur.getPrevHash() != prev.getHash())
            {
                return cur.getIndex();
            }
        }
    }
    return BigNumber();
}

// Blocks //

Block Blockchain::getLastBlock()
{
    Block block = fileMode ? blockIndex.getLastBlock() : memIndex.getLastBlock();
    return validateAndReturnBlock(block);
}

QByteArray Blockchain::getBlockDataByIndex(const BigNumber &index)
{
    return blockIndex.getBlockDataById(index);
}

Block Blockchain::getBlockByApprover(const BigNumber &approver)
{
    Block block = fileMode ? memIndex.getByApprover(approver) : blockIndex.getBlockByApprover(approver);
    return validateAndReturnBlock(block);
}

Block Blockchain::getBlockByData(const QByteArray &data)
{
    Block block = fileMode ? blockIndex.getBlockByData(data) : memIndex.getByData(data);
    return validateAndReturnBlock(block);
}

Block Blockchain::getBlockByHash(const QByteArray &hash)
{
    Block block = fileMode ? blockIndex.getBlockByHash(hash) : memIndex.getByHash(hash);
    return validateAndReturnBlock(block);
}

Transaction Blockchain::getTxByHash(const QByteArray &hash, const QByteArray &token)
{
    return fileMode ? blockIndex.getLastTxByHash(hash, token) : memIndex.getLastTxByHash(hash, token);
}

Transaction Blockchain::getTxBySender(const BigNumber &id, const QByteArray &token)
{
    return fileMode ? blockIndex.getLastTxBySender(id, token) : memIndex.getLastTxBySender(id, token);
}

Transaction Blockchain::getTxByReceiver(const BigNumber &id, const QByteArray &token)
{
    return fileMode ? blockIndex.getLastTxByReceiver(id, token) : memIndex.getLastTxByReceiver(id, token);
}

Transaction Blockchain::getTxBySenderOrReceiver(const BigNumber &id, const QByteArray &token)
{
    return fileMode ? blockIndex.getLastTxBySenderOrReceiver(id, token)
                    : memIndex.getLastTxBySenderOrReceiver(id, token);
}

Transaction Blockchain::getTxBySenderOrReceiverAndToken(const BigNumber &id, const QByteArray &token)
{
    return fileMode ? blockIndex.getLastTxBySenderOrReceiverAndToken(id, token)
                    : memIndex.getLastTxBySenderOrReceiverAndToken(id, token);
}

Transaction Blockchain::getTxByApprover(const BigNumber &id, const QByteArray &token)
{
    return fileMode ? blockIndex.getLastTxByApprover(id, token) : memIndex.getLastTxByApprover(id, token);
}

Transaction Blockchain::getTxByUser(const BigNumber &id, const QByteArray &token)
{
    return fileMode ? blockIndex.getLastTxByApprover(id, token) : memIndex.getLastTxByApprover(id, token);
}

TxPair Blockchain::getTxPair(const BigNumber &first, const BigNumber second)
{
    return fileMode ? blockIndex.searchPair(first, second) : memIndex.searchPair(first, second);
}

QList<Transaction> Blockchain::getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from, int count,
                                                             BigNumber token)
{
    return /*fileMode ?*/ blockIndex.getTxsBySenderOrReceiverInRow(id, from, count, token);
    // : memIndex.getLastTxBySenderOrReceiver(id);
}

void Blockchain::getBlockZero()
{
    Block zero = getBlockByIndex(0);
    if (zero.isEmpty())
    {
        Messages::GetBlockMessage request(SearchEnum::BlockParam::Id, QByteArray::number(0));
        emit sendMessage(request.serialize(), Messages::GET_BLOCK_MESSAGE);
    }
    else
        actorIndex->setCompanyId(new QByteArray(zero.getApprover().toActorId()));
}

// Genesis block //

bool Blockchain::shouldStartGenesisCreation()
{
    return Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS == this->blocksFromLastGenesis;
}

BigNumber Blockchain::getBalanceFromTx(BigNumber id, Transaction tx)
{

    /*NEED TO BE REDONE*/

    if (tx.getReceiver() == id)
        return tx.getReceiverBalance() + tx.getAmount();
    else if (tx.getSender() == id)
        return tx.getSenderBalance() - tx.getAmount();
    else
        return 0;
}

void Blockchain::addRecordsIfNew(const GenesisDataRow &row1, const GenesisDataRow &row2)
{
    bool b1 = false;
    bool b2 = false;
    for (int i = 0; i < genBlockData.size(); i++)
    {
        if (genBlockData[i].actorId == row1.actorId && genBlockData[i].token == row1.token)
        {
            b1 = true;
        }
        if (genBlockData[i].actorId == row2.actorId && genBlockData[i].token == row2.token)
        {
            b2 = true;
        }
        if (b1 && b2)
        {
            return;
        }
        else
        {
            if (!b1)
            {
                genBlockData.append(row1);
            }
            if (!b2)
            {
                genBlockData.append(row2);
            }
            return;
        }
    }
}

QByteArray Blockchain::findRecordsInBlock(const Block &block)
{
    if (block.getType() == Config::GENESIS_BLOCK_TYPE)
    {
        return block.getHash();
    }
    else if (!block.isEmpty())
    {
        for (const Transaction &tx : block.extractTransactions())
        {
            if (tx.getReceiver() == BigNumber(*actorIndex->companyId))
                break;
            GenesisDataRow recSender =
                GenesisDataRow(tx.getSender(), tx.getSenderBalance() - tx.getAmount(), tx.getToken());
            GenesisDataRow recReceiver =
                GenesisDataRow(tx.getReceiver(), tx.getReceiver() - tx.getAmount(), tx.getToken());
            addRecordsIfNew(recReceiver, recSender);
        }
    }
    return QByteArray();
}

GenesisBlock Blockchain::createGenesisBlock(const Actor<KeyPrivate> actor, QMap<BigNumber, BigNumber> states)
{
    qDebug() << "Creating genesis block";
    genBlockData.clear();
    QByteArray previousGenHash;
    GenesisBlock nb("", Block(), "");
    if (fileMode)
    {
        if (blockIndex.getLastSavedId().isEmpty())
        {
            qCritical() << "Can't create genesis block, there no last saved id";
            return nb;
        }
        if (blockIndex.getRecords() == 0)
        {

            if (blockIndex.getFirstSavedId() == 0 && blockIndex.getLastSavedId() == 0)
            {

                for (QMap<BigNumber, BigNumber>::iterator i = states.begin(); i != states.end(); i++)
                {
                    genBlockData.append(GenesisDataRow(i.key(), i.value(), 0));
                }
                BigNumber comp = BigNumber(*(actorIndex->companyId));
                //                nb.setApprover(BigNumber(*(actorIndex->companyId)));
                nb.sign(accountController->getActor(comp));
            }
            else
                qCritical() << "Can't create genesis block, there no blocks in blockIndex";
            return nb;
        }
        else
        {
            Block b;
            BigNumber i = blockIndex.getLastSavedId();
            nb = GenesisBlock("", blockIndex.getBlockById(blockIndex.getLastSavedId()), "");
            while ((blockIndex.getBlockById(i).getType() != Config::GENESIS_BLOCK_TYPE)
                   && (i >= blockIndex.getFirstSavedId()))
            {
                b = blockIndex.getBlockById(i);
                findRecordsInBlock(b);
                i--;
            }
            foreach (GenesisDataRow dr, genBlockData)
            {
                nb.addRow(dr);
            }
            nb.setPrevGenHash(blockIndex.getBlockById(i).getHash());
        }
        qDebug() << "Genesis block created";
        genBlockData.clear();
        nb.sign(actor);
        return nb;
    }
    else
        return GenesisBlock();
}
// Merging //

int Blockchain::mergeBlockWithLocal(const Block &received)
{

    Block existed = getBlockByIndex(received.getIndex());
    if (canMergeBlocks(received, existed))
    {
        qWarning() << "Blocks with id" << received.getIndex() << "can't be merged";
        return Errors::BLOCKS_CANT_MERGE;
    }

    qDebug()
        << QString("Start merging block [%1] with exising [%2]").arg(received.toString(), existed.toString());
    if (received == existed)
    { // Non-approved code
        qDebug() << QString("Blocks are equal ([%1])").arg(Errors::BLOCKS_ARE_EQUAL);
        return Errors::BLOCKS_ARE_EQUAL;
    }
    if (received.contain(existed))
    {
        removeBlock(existed);
        int res = addBlock(received);
        return res;
    }

    // step 1 - create merged block
    Block merged = mergeBlocks(received, existed);

    if (merged.isEmpty())
        return Errors::BLOCKS_CANT_MERGE;

    // step 2 - collect all blocks from old to latest
    QList<Block> tmpBlocks; // from existed to last block;

    // only if indexes is different
    if (received.getIndex() != getLastBlock().getIndex())
    {
        // we should collect temp blocks
        BigNumber lastBlockId = existed.getIndex();
        BigNumber nextBlockId = getLastBlock().getIndex();
        for (BigNumber i = lastBlockId; i <= nextBlockId; i++)
        {
            tmpBlocks << getBlockByIndex(i);
        }
        if (tmpBlocks.isEmpty())
        {
            qWarning() << "Error: There is no blocks found locally while merging block"
                       << received.getIndex();
            return Errors::NO_BLOCKS;
        }
    }

    // step 3 - update hash, prevHash and approver for all modified blocks
    QByteArray newHash = merged.getHash();
    QByteArray oldHash = existed.getHash();
    for (Block &b : tmpBlocks)
    {
        if (b.getPrevHash() == oldHash)
        {
            oldHash = b.getHash();
            b.setPrevHash(newHash);
            b.setType(Config::MERGE_BLOCK);
            signBlock(b);
            newHash = b.getHash();
        }
    }

    // step 4 - remove existed block (and all blocks after them)
    // and save updated blocks with new hash
    removeBlock(existed);
    addBlock(merged);
    for (const Block &b : tmpBlocks)
    {
        addBlock(b);
    }
    //  emit SendMergedBlock(existed, received, merged);
    return 0;
}

int Blockchain::mergeGenesisBlockWithLocal(const GenesisBlock &received)
{
    GenesisBlock existed = blockIndex.getGenesisBlockById(received.getIndex());
    if (!existed.isEmpty())
    {
        // saved block with the same id is genesis
        qDebug() << QString("Start merging genesis block [%1] with exising [%2]")
                        .arg(received.toString(), existed.toString());

        // step 1
        GenesisBlock merged = mergeGenesisBlocks(received, existed);

        // step 2 - collect all blocks from old to latest
        QList<Block> tmpBlocks; // from existed to last block;

        // only if indexes is different
        if (received.getIndex() != getLastBlock().getIndex())
        {
            // we should collect temp blocks
            BigNumber lastBlockId = existed.getIndex();
            BigNumber nextBlockId = getLastBlock().getIndex();
            for (BigNumber i = lastBlockId; i <= nextBlockId; i++)
            {
                tmpBlocks << getBlockByIndex(i);
            }
            if (tmpBlocks.isEmpty())
            {
                qWarning() << "Error: There is no blocks found locally while merging block"
                           << received.getIndex();
                return Errors::NO_BLOCKS;
            }
        }

        // step 3 - update hash, prevHash and approver for all modified blocks
        QByteArray newHash = merged.getHash();
        QByteArray oldHash = existed.getHash();
        for (Block &b : tmpBlocks)
        {
            if (b.getPrevHash() == oldHash)
            {
                oldHash = b.getHash();
                b.setPrevHash(newHash);
                b.setType(Config::GENESIS_BLOCK_MERGE);
                signBlock(b);
                newHash = b.getHash();
            }
        }

        // step 4 - remove existed block (and all blocks after them)
        // and save updated blocks with new hash
        removeBlock(existed);
        addBlock(merged, true);
        for (const Block &b : tmpBlocks)
        {
            addBlock(b);
        }
    }
    else
    {
        qCritical() << "Can't find genesis block with id=" << received.getIndex() << "locally";
        return Errors::NO_BLOCKS;
    }
    return 0;
}

Block Blockchain::getBlock(SearchEnum::BlockParam type, const QByteArray &value)
{
    Block res;
    switch (type)
    {
    case SearchEnum::BlockParam::Id:
        res = getBlockByIndex(BigNumber(value));
        break;
    case SearchEnum::BlockParam::Data:
        res = getBlockByData(value);
        break;
    case SearchEnum::BlockParam::Hash:
        res = getBlockByHash(value);
        break;
    case SearchEnum::BlockParam::Approver:
        res = getBlockByApprover(BigNumber(value));
        break;
    default:
        res = Block();
        break;
    }
    return res;
}

QByteArray Blockchain::getBlockData(SearchEnum::BlockParam type, const QByteArray &value)
{
    QByteArray res = "";
    switch (type)
    {
    case SearchEnum::BlockParam::Id:
        res = getBlockDataByIndex(BigNumber(value));
        break;
    default:
        break;
    }
    return res;
}

Transaction Blockchain::getTransaction(SearchEnum::TxParam type, const QByteArray &value)
{
    switch (type)
    {
    case SearchEnum::TxParam::UserSenderOrReceiverOrToken:
        return getTxBySenderOrReceiverAndToken(value);
    case SearchEnum::TxParam::Hash:
        return getTxByHash(value);
    case SearchEnum::TxParam::User:
        return getTxByUser(BigNumber(value));
    case SearchEnum::TxParam::UserApprover:
        return getTxByApprover(BigNumber(value));
    case SearchEnum::TxParam::UserReceiver:
        return getTxByReceiver(BigNumber(value));
    case SearchEnum::TxParam::UserSender:
        return getTxBySender(BigNumber(value));
    case SearchEnum::TxParam::UserSenderOrReceiver:
        return getTxBySenderOrReceiver(BigNumber(value));
    default:
        qWarning() << "Can't get tx by Null and value:" + value;
        return Transaction();
    }
}

bool Blockchain::validateBlock(const Block &block)
{
    return actorIndex->validateBlock(block);
}

Block Blockchain::validateAndReturnBlock(const Block &block)
{
    // Get prev block hash and check if it exists in current one :)
    return block;
}

int Blockchain::addBlock(const Block &block, bool isGenesis)
{
    if (isGenesis)
    {
        qDebug() << "Adding a GENESIS block" << block.getIndex() << "to storage";
    }
    else
    {
        qDebug() << "Adding a block" << block.getIndex() << "to storage";
    }
    if (!GenesisBlock::isGenesisBlock(block.serialize()))
    {
        if (block.getIndex() != 0)
        {
            BigNumber id = block.getIndex() - 1;
            if (getBlock(SearchEnum::BlockParam::Id, id.toByteArray()).isEmpty())
            {
                Messages::GetBlockMessage request(SearchEnum::BlockParam::Id, id.toByteArray());
                emit sendMessage(request.serialize(), Messages::GET_BLOCK_MESSAGE);
            }
        }
    }
    if (block.getIndex() == 0)
    {
        this->actorIndex->setCompanyId(new QByteArray(block.getApprover().toActorId()));
    }
    int resultCode = fileMode ? blockIndex.addBlock(block) : memIndex.addBlock(block);

    switch (resultCode)
    {
    case 0:
    {
        emit updateLastTransactionList(); // TODO: ?
        qDebug() << "Block" << block.getIndex() << block.getType() << "is successfully added to blockchain";
        getSmContractMembers(block);
        emit sendMessage(block.serialize(), block_message);
        break;
    }
    case Errors::FILE_ALREADY_EXISTS:
    {
        qDebug() << "Block" << block.getIndex() << "is already in blockchain";
        if ((block.getType() == Config::DATA_BLOCK_TYPE) || block.getType() == Config::MERGE_BLOCK)
        {
            resultCode = mergeBlockWithLocal(block);
        }
        else if ((block.getType() == Config::GENESIS_BLOCK_TYPE)
                 || (block.getType() == Config::GENESIS_BLOCK_MERGE))
        {
            resultCode = mergeGenesisBlockWithLocal(dynamic_cast<const GenesisBlock &>(block));
        }
        else
        {
            qCritical() << "Unsupported block type in block: " << block.getIndex();
        }
        break;
    }
    default:
        qCritical() << "While adding a new block" << block.toString();
    }

    // after adding genesis block we don't need to increment counter
    if (!isGenesis && resultCode == 0)
    {
        blocksFromLastGenesis++;
        if (shouldStartGenesisCreation())
        {
            createGenesisBlock(*(accountController->getMainActor()));
        }
    }

    return resultCode;
}

int Blockchain::removeBlock(const Block &block)
{
    return fileMode ? blockIndex.removeById(block.getIndex()) : memIndex.removeById(block.getIndex());
}

bool Blockchain::canMergeBlocks(const Block &blockA, const Block &blockB)
{
    // 1) Blocks are approved
    // 2) Blocks has one type
    // 3) Blocks ids are identical
    if (!blockA.getDigSig().isEmpty() && !blockB.getDigSig().isEmpty() && blockA.getType() == blockB.getType()
        && blockA.getIndex() == blockB.getIndex())
    {
        if ((blockA.getType() == Config::DATA_BLOCK_TYPE) || (blockA.getType() == Config::GENESIS_BLOCK_TYPE))
            return true;
        else if (blockA.getType() == Config::GENESIS_BLOCK_MERGE)
        {
            // 4) at least one common data row
            QList<GenesisDataRow> rowsA = dynamic_cast<const GenesisBlock &>(blockA).extractDataRows();
            QList<GenesisDataRow> rowsB = dynamic_cast<const GenesisBlock &>(blockB).extractDataRows();
            for (const GenesisDataRow &g : rowsA)
            {
                if (rowsB.contains(g))
                {
                    return true;
                }
            }
        }
        else if (blockA.getType() == Config::MERGE_BLOCK)
        {
            // 4) at least one common transaction
            QList<Transaction> transactionsA = blockA.extractTransactions();
            QList<Transaction> transactionsB = blockB.extractTransactions();
            for (const Transaction &tr : transactionsA)
            {
                if (transactionsB.contains(tr))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

Block Blockchain::mergeBlocks(const Block &blockA, const Block &blockB)
{
    qDebug() << "Attempting to merge block:" << blockA.serialize() << "and block:" << blockB.serialize();

    if (blockA.getIndex() == BigNumber(0))
        return Block();
    Block prev = getBlockByIndex(blockA.getIndex() - 1);
    if (prev.isEmpty())
    {
        qWarning() << "Can't merge block" << blockA.toString() << "with" << blockB.toString()
                   << " - there no prev block";
        return Block();
    }

    const QByteArray dataA = blockA.getData();
    const QByteArray dataB = blockB.getData();

    // Case 1 - equal payload
    if (dataA == dataB)
    {
        Block merged(dataA, prev);
        signBlock(merged);
        return merged;
    }
    else // Case 2 - different payload
    {
        QList<Transaction> transactionsA = blockA.extractTransactions();
        QList<Transaction> transactionsB = blockB.extractTransactions();

        //        ListContainer<Transaction> txs;
        QList<Transaction> resultList = transactionsA;

        for (const Transaction &tx : transactionsA)
        {
            if (!transactionsB.contains(tx))
            {
                resultList.append(tx);
            }
        }
        QList<QByteArray> list;
        for (const Transaction &tx : resultList)
            list << tx.serialize();
        QByteArray dataBlock = Serialization::universalSerialize(list);
        Block mergedBlock(dataBlock, prev);
        signBlock(mergedBlock);
        return mergedBlock;
    }
}

GenesisBlock Blockchain::mergeGenesisBlocks(const GenesisBlock &blockA, const GenesisBlock &blockB)
{
    qDebug() << "Attempting to merge genesis block:" << blockA.serialize()
             << "and block:" << blockB.serialize();

    Block prev = getBlockByIndex(blockA.getIndex() - 1);

    if (prev.isEmpty())
    {
        qWarning() << "Can't merge genesis block" << blockA.toString() << "with" << blockB.toString()
                   << " - there no prev block";
        return GenesisBlock();
    }

    const QByteArray dataA = blockA.getData();
    const QByteArray dataB = blockB.getData();

    // Case 1 - equal payload
    if (dataA == dataB)
    {
        GenesisBlock merged(dataA, prev, blockA.getPrevGenHash());
        signBlock(merged);
        return merged;
    }
    else // Case 2 - different payload
    {
        // todo: make utils::combine(list, list) function;
        QList<GenesisDataRow> genDataRowsA = blockA.extractDataRows();
        QList<GenesisDataRow> genDataRowsB = blockB.extractDataRows();
        QList<GenesisDataRow> resultList = genDataRowsA;
        int count = 0;
        for (const GenesisDataRow &r : genDataRowsA)
        {
            if (!genDataRowsB.contains(r))
            {
                resultList.append(r);
            }
            else
                count++;
        }
        QList<QByteArray> list;
        if (count < Config::NECESSARY_SAME_TX)
            return GenesisBlock();
        for (const GenesisDataRow &gn : resultList)
            list << gn.serialize();
        QByteArray genData = Serialization::universalSerialize(list);
        GenesisBlock mergedBlock(genData, prev, blockA.getPrevGenHash());
        signBlock(mergedBlock);
        return mergedBlock;
    }
}

void Blockchain::signBlock(Block &block) const
{
    block.sign(accountController->getCurrentActor());
}

BigNumber Blockchain::getBlockChainLength() const
{
    return fileMode ? blockIndex.getRecords() : memIndex.getRecords();
}

QString Blockchain::getLastBlockData() const
{
    return fileMode ? blockIndex.getLastBlock().getData() : memIndex.getLastBlock().getData();
}

BigNumber Blockchain::getRecords() const
{
    return fileMode ? blockIndex.getRecords() : memIndex.getRecords();
}

BigNumber Blockchain::getUserBalance(BigNumber userId, BigNumber tokenId) const
{

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--)
    {
        Block currentBlock = blockIndex.getBlockById(i);

        if (currentBlock.isEmpty())
            break;

        QList<Transaction> txs = currentBlock.extractTransactions();

        for (auto &tx : txs)
        {
            if (tx.getSender() == userId && tx.getToken() == tokenId)
            {
                return tx.getSenderBalance() - tx.getAmount();
            }
            else if (tx.getReceiver() == userId && tx.getToken() == tokenId)
            {
                return tx.getReceiverBalance() + tx.getAmount();
            }
        }
    }

    return BigNumber(0);
}

void Blockchain::showBlockchain() const
{
    qDebug() << "BLOCKCHAIN: showBlockchain()";
    int i = 0;
    Block currentBlock = blockIndex.getBlockById(i);
    do
    {
        //        qDebug() << "     Block id: " << currentBlock.getIndex()
        //                 << "\n     Aprover: " << currentBlock.getApprover()
        //                 << "\n     Data: " << currentBlock.getData()
        //                 << "\n     Hash: " << currentBlock.getHash()
        //                 << "\n     PrevHash: " << currentBlock.getPrevHash()
        //                 << "\n     DigSig: " << currentBlock.getDigSig();
        i++;
        currentBlock = blockIndex.getBlockById(i);
    } while (!currentBlock.isEmpty());
    GenesisBlock genBlock = blockIndex.getLastGenesisBlock();
    qDebug() << "Genesis block: ";
    for (auto dataGen : genBlock.extractDataRows())
    {
        qDebug() << &dataGen;
    }
}

bool Blockchain::isSmContractTx(const Block &block) const
{
    if (block.getData().contains("initcontract"))
        return true;
    return false;
}

void Blockchain::getSmContractMembers(const Block &block) const
{
    if (!isSmContractTx(block))
        return;
    QList<Transaction> txList = block.extractTransactions();
    for (const Transaction &tx : txList)
    {
        if (tx.getData() == "initcontract")
        {
            actorIndex->getActor(tx.getSender());
            actorIndex->getActor(tx.getReceiver());
        }
    }
}

void Blockchain::process()
{
    //
}

void Blockchain::updateBlockchain(BigNumber id, bool isUser)
{
    Messages::BlockCount request;
    emit sendMessage(request.serialize(), Messages::GET_BLOCK_COUNT_MESSAGE);
}

void Blockchain::updateBlockchainForSignIn(QByteArray id, QByteArrayList idList)
{
    Messages::BlockCount request;
    emit sendMessage(request.serialize(), Messages::GET_BLOCK_COUNT_MESSAGE);
}

void Blockchain::checkBlockExistence(const Block &block)
{
    Block last = getLastBlock();

    /*
     * Blocks in blockchain are stored consistently, so if last block id
     * is greater than the coming block id - the last one is already in
     * blockchain. If ids are equals - trying to merge blocks.
     */
    if (last.getIndex() < block.getIndex() || last.isEmpty())
    {
        addBlock(block);
        QList<Transaction> tempTxList = block.extractTransactions();
        foreach (Transaction tx, tempTxList)
        {
            if (accountController->sentTxList.at(tx.getHash()) != "")
            {
                accountController->sentTxList.remove(tx.getHash());
                break;
            }
        }
        emit BlockIsMissing(block);
    }
    else if (last.getIndex() < block.getIndex())
    {
        qDebug() << QString("Block [%1] already exists in local blockchain")
                        .arg(QString(block.getIndex().toByteArray()));
    }
    else if (last.getIndex() == block.getIndex())
    {
        // blocks id's are equals -> merge blocks
        if (canMergeBlocks(last, block))
        {
            Block merged = mergeBlocks(last, block);
            if (merged.isEmpty())
                return;
            addBlock(merged);
            //      emit SendMergedBlock(last, block, merged);
        }
    }
}

void Blockchain::blockCountResponse(const BigNumber &count)
{
    if (blockIndex.getLastSavedId() < count
        || getBlock(SearchEnum::BlockParam::Id, count.toByteArray()).isEmpty())
    {
        Messages::GetBlockMessage request(SearchEnum::BlockParam::Id, count.toByteArray());
        emit sendMessage(request.serialize(), get_block_message);
    }
}

void Blockchain::getBlockFromBlockchain(const SearchEnum::BlockParam &param, const QByteArray &value,
                                        const QByteArray &requestHash, const SocketPair &receiver)
{
    QByteArray srBlock = getBlockData(param, value);
    if (srBlock.isEmpty())
        return;
    emit responseReady(srBlock, Messages::GET_BLOCK_RESPONSE_MESSAGE, requestHash, receiver);
}

void Blockchain::getBlockCount(const QByteArray &requestHash, const SocketPair &receiver)
{
    qDebug() << "BLOCKCHAIN: getBlockCount() count - " << this->blockIndex.getLastSavedId();

    emit responseReady(this->blockIndex.getLastSavedId().toByteArray(),
                       Messages::GET_BLOCK_COUNT_RESPONSE_MESSAGE, requestHash, receiver);
}

void Blockchain::addBlockToBlockchain(Block block)
{
    addBlock(block);
}

void Blockchain::addGenBlockToBlockchain(const GenesisBlock &block)
{
    if (block.getIndex() == 0)
    {
        mutex.lock();
        this->actorIndex->setCompanyId(new QByteArray(block.getApprover().toActorId()));
        mutex.unlock();
    }
    if (blockIndex.addBlock(block) == 0)
        sendMessage(block.serialize(), Messages::GENESIS_BLOCK_MESSAGE);
}

// Actors //

int Blockchain::addActor(const Actor<KeyPublic> &actor)
{
    //    return actorIndex->addActor(actor);
    return 0;
}

Actor<KeyPublic> Blockchain::getActor(const BigNumber &actorId)
{
    return actorIndex->getActor(actorId);
}

Actor<KeyPrivate> Blockchain::getApprover() const
{
    return accountController->getCurrentActor();
}

void Blockchain::setApprover(const Actor<KeyPrivate> &value)
{
    this->accountController->getCurrentActor() = value;
}

void Blockchain::getTxFromBlockchain(const SearchEnum::TxParam &param, const QByteArray &value,
                                     const SocketPair &receiver, const QByteArray &request)
{
    Transaction transaction = getTransaction(param, value);
    if (!transaction.isEmpty())
    {
        emit responseReady(transaction.serialize(), Messages::GET_TX_RESPONSE_MESSAGE, request, receiver);
    }
    else
    {
        qDebug() << "The transaction with" << SearchEnum::toString(param) << "parametr is not found";
    }
}

void Blockchain::VerifyTx(Transaction tx)
{
    Block last = getLastBlock();
    QList<Transaction> lastBlockTxs = last.extractTransactions();

    // check txs in the last block
    if (lastBlockTxs.contains(tx))
    {
        qDebug() << "New transaction can't be added: previous block contains it";
        return;
    }

    qDebug() << QString("New transaction [%1] is verified").arg(tx.toString());
    emit VerifiedTx(tx);
}

void Blockchain::proveTx()
{
    qDebug() << "proveTx: started";
    QObject *s = QObject::sender();
    Transaction *tx = qobject_cast<Transaction *>(s);
    //    if (tx->getSender() == BigNumber(*actorIndex->companyId))
    //    {
    BigNumber targetSender = tx->getSender();
    Actor<KeyPublic> senderActor = actorIndex->getActor(targetSender);
    if (senderActor.isEmpty())
    {
        qDebug() << "Tx" << tx->getHash() << "not approved: no such actor";
        emit tx->NotApproved();
        return;
    }
    bool sig = senderActor.getKey()->verify(tx->getDataForDigSig(), tx->getDigSig());
    if (!sig)
    {
        qDebug() << "Tx" << tx->getHash() << "not approved: bad signature";
        emit tx->NotApproved();
        return;
    }
    //        if (sig)
    //        {
    //            emit tx->Approved();
    //            return;
    //        }
    //        else
    //        {
    //            emit tx->NotApproved();
    //            qDebug() << "Transaction not approved: false zero user";
    //            return;
    //        }
    //    }
    if (tx->getData() == "genesis")
    {
        // type = 6, token = correct
        //        Profile profile = actorIndex->getProfile(tx->getSender().toActorId());

        //        if (profile.type() != 6)
        //        {
        //            emit tx->NotApproved();
        //            qDebug() << "Transaction not approved: genesis block is not from contract";
        //            return;
        //        }

        if (tx->getSender() != tx->getToken())
        {
            emit tx->NotApproved();
            qDebug() << "Transaction not approved: sender != token in genesis block";
            return;
        }
        emit tx->Approved();
        return;
    }

    if (tx->getSender() == tx->getReceiver())
    {
        emit tx->NotApproved();
        qDebug() << "Transaction not approved: sender == receiver";
        return;
    }
    if (tx->getAmount() <= 0 && targetSender.toActorId() != *actorIndex->companyId)
    {
        emit tx->NotApproved();
        qDebug() << "Transaction not approved: amount <= 0";
        return;
    }

    // verify sender state
    //    BigNumber targetSender = tx->getSender();
    //    Actor<KeyPublic> senderActor = actorIndex->getActor(targetSender);
    if (senderActor.isEmpty())
    {
        emit tx->NotApproved();
        return;
    }
    if (tx->getData() == "initcontract")
    {
        qDebug() << "Contract tx proving";
        QByteArrayList profile = senderActor.profile().getListProfile();
        if (profile[0] == "6" && profile[5] == tx->getReceiver())
        {
            qDebug() << "Contract tx proved";
            tx->sign(accountController->getCurrentActor());
            emit tx->Approved();
            return;
        }
    }
    BigNumber senderCurBal = 0;
    bool senderBalanceIsValid = false;
    if (targetSender.toActorId() != *actorIndex->companyId)
    {
        Transaction senderLastTx = getTxBySenderOrReceiver(targetSender, tx->getToken().toActorId());
        senderCurBal = getBalanceFromTx(targetSender, senderLastTx);

        if (tx->getSenderBalance() == senderCurBal)
            senderBalanceIsValid = true;
    }
    else
        senderBalanceIsValid = true;

    // verify receiver state
    BigNumber targetReceiver = tx->getReceiver();
    Transaction receiverLastTx = getTxBySenderOrReceiver(targetReceiver, tx->getToken().toActorId());
    BigNumber receiverCurBal = getBalanceFromTx(targetReceiver, receiverLastTx);
    bool receiverBalanceIsValid = false;
    if (tx->getReceiverBalance() == receiverCurBal)
        receiverBalanceIsValid = true;

    // qDebug() << "ASDASDASD" << senderCurBal - tx->getAmount();
    if (senderCurBal - tx->getAmount() < 0
        && targetSender.toActorId() != *actorIndex->companyId /*|| receiverCurBal + tx->getAmount() < 0*/)
    {
        qDebug() << senderCurBal << tx->getAmount();
        qDebug() << "Transaction "
                    "not approved: sender's or receiver's balance will be < 0";
        emit tx->NotApproved();
        return;
    }

    if (senderBalanceIsValid && receiverBalanceIsValid)
    {
        tx->sign(accountController->getCurrentActor());
        emit tx->Approved();
        return;
    }
    else
    {
        emit tx->NotApproved();
        qDebug() << "Transaction not approved: balance not valid";
    }
}

// Other //

void Blockchain::setMode(bool fileMode)
{
    this->fileMode = fileMode;
}

ActorIndex *Blockchain::getActorIndex()
{
    return actorIndex;
}

MemIndex &Blockchain::getMemIndex()
{
    return memIndex;
}

BlockIndex &Blockchain::getBlockIndex()
{
    return blockIndex;
}

void Blockchain::removeAll()
{
    this->actorIndex->removeAll();
    this->memIndex.removeAll();
    this->blockIndex.removeAll();
    QFile(DataStorage::TMP_GENESIS_BLOCK).remove();
}
