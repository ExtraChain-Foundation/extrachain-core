#include "datastorage/blockchain.h"

Blockchain::Blockchain(AccountController *accountController, bool fileMode)
    : fileMode(fileMode)
    , accountController(accountController)
{
    actorIndex = accountController->getActorIndex();
    connect(this, &Blockchain::NewBlock, this,
            &Blockchain::BlockIsMissing); // non-approved code
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

Block Blockchain::getBlockByIndex(const BigNumber &index)
{
    Block block = fileMode ? blockIndex.getBlockById(index) : memIndex[index];
    return validateAndReturnBlock(block);
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

// Genesis block //

bool Blockchain::shouldStartGenesisCreation()
{
    return Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS <= this->blocksFromLastGenesis;
}

BigNumber Blockchain::getBalanceFromTx(BigNumber id, Transaction tx)
{
    if (tx.getReceiver() == id)
        return tx.getReceiverBalance() + tx.getAmount();
    else if (tx.getSender() == id)
        return tx.getSenderBalance() - tx.getAmount();
    else
        return 0;
}

void Blockchain::createGenesisBlock()
{
    using namespace std;
    qDebug() << "Creating genesis block";

    // temporary file storage for constructed genesis block
    QFile file(DataStorage::TMP_GENESIS_BLOCK);
    if (!FileSystem::tryToOpen(file, QIODevice::WriteOnly))
    {
        qCritical() << "Error while creating genesis block, can't create tmp file";
        return;
    }

    QDataStream writeStream(&file);

    QMultiMap<BigNumber, BigNumber> alreadySaved; // actorid -> token

    // function to check should we add a new record to map (actorId -> tx)
    // @return true if @param tx should be added to the collectedData map
    auto isNewTxRecord = [&alreadySaved](const GenesisDataRow &row) -> bool {
        if (alreadySaved.contains(row.actorId))
        {
            return !alreadySaved.values(row.actorId).contains(row.tx.getToken()); // true = there no tx with
                                                                                  // such data field yet
        }
        return true;
    };

    auto addRecordIfNew = [&](const GenesisDataRow &row) {
        if (isNewTxRecord(row))
        {
            writeStream << row.serialize();
            alreadySaved.insert(row.actorId, row.tx.getToken());
        }
    };

    // @return previous genesis block hash (if found)
    auto findRecordsInBlock = [&](const Block &block) -> QByteArray {
        if (block.getType() == Config::GENESIS_BLOCK_TYPE)
        {
            return block.getHash();
        }
        else if (!block.isEmpty())
        {
            for (const Transaction &tx : block.extractTransactions())
            {
                if (tx.getReceiver() == BigNumber("0"))
                    break;
                GenesisDataRow recSender = GenesisDataRow(tx.getSender(), tx);
                addRecordIfNew(recSender);
                GenesisDataRow recReceiver = GenesisDataRow(tx.getReceiver(), tx);
                addRecordIfNew(recReceiver);
            }
        }
        return QByteArray();
    };

    QByteArray previousGenHash;

    // Collect data to collectedData map (block index or file index)
    if (fileMode)
    {
        if (blockIndex.getRecords() == 0)
        {
            qCritical() << "Can't create genesis block, there no blocks in blockIndex";
            return;
        }

        BigNumber lastSavedId = blockIndex.getLastSavedId();
        if (lastSavedId.isEmpty())
        {
            qCritical() << "Can't create genesis block, there no last saved id";
            return;
        }

        // Collect data: Iterating from last block to previous genesis block
        for (BigNumber i = lastSavedId - 1; i > 0; --i)
        {
            Block block = blockIndex.getBlockById(i);
            QByteArray prevGenHash = findRecordsInBlock(block);
            if (!prevGenHash.isEmpty())
            {
                previousGenHash = prevGenHash;
                break;
            }
        }
    }
    else
    {
        if (memIndex.getRecords() == 0)
        {
            qCritical() << "Can't create genesis block, there no blocks in memIndex";
            return;
        }

        BigNumber lastSavedId = memIndex.getLastBlock().getIndex();
        if (lastSavedId.isEmpty())
        {
            qCritical() << "Can't create genesis block, there no last saved id";
            return;
        }

        for (BigNumber i = lastSavedId - 1; i > 0; --i)
        {
            QByteArray prevGenHash = findRecordsInBlock(memIndex[i]);
            if (!prevGenHash.isEmpty())
            {
                previousGenHash = prevGenHash;
                break;
            }
        }
    }

    file.close();

    Block prevBlock = getLastBlock();

    if (fileMode)
    {
        addGenesisBlockFromTempFile(previousGenHash);
    }

    emit GenesisBlockCreated(prevBlock, previousGenHash);
}

void Blockchain::addGenesisBlockFromTempFile(const QByteArray &prevGenesisHash)
{
    if (prevGenesisHash.isEmpty())
    {
        qDebug() << "Creating first genesis block";
    }
    else
    {
        qDebug() << "Adding new genesis block from tmp/genesis file";
        qDebug() << "Last genesis block hash: " << prevGenesisHash;
    }

    Block lastBlock = getLastBlock();
    GenesisBlock *genBlock = readGenesisBlock(lastBlock, prevGenesisHash);
    if (genBlock == nullptr)
    {
        qCritical() << "Error while adding genesis block";
        return;
    }

    blocksFromLastGenesis = 0;

    signBlock(*genBlock);
    addBlock(*genBlock, true);
    delete genBlock;
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
        emit NewBlock(received);
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
    emit NewBlock(merged); // Non-approved code
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
    switch (type)
    {
    case SearchEnum::BlockParam::Id:
        return getBlockByIndex(BigNumber(value));
    case SearchEnum::BlockParam::Data:
        return getBlockByData(value);
    case SearchEnum::BlockParam::Hash:
        return getBlockByHash(value);
    case SearchEnum::BlockParam::Approver:
        return getBlockByApprover(BigNumber(value));
    default:
        return Block();
    }
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
    //    if (validateBlock(block))
    //    {
    return block;
    //    }
    //    else
    //    {
    //        qWarning() << "Block:" << block.getIndex() << "it is corrupted";
    //        emit BlockCorrupted(block);
    //        return Block();
    //    }
}

GenesisBlock *Blockchain::readGenesisBlock(const Block &prevBlock, const QByteArray &prevGenesisHash)
{
    QFile file(DataStorage::TMP_GENESIS_BLOCK);
    if (!FileSystem::tryToOpen(file, QIODevice::ReadOnly))
    {
        qWarning() << "Error while reading from genesis block temp file";
        return nullptr;
    }
    QDataStream readStream(&file);
    GenesisBlock *genBlock = new GenesisBlock("", &prevBlock, prevGenesisHash);
    while (!readStream.atEnd())
    {
        GenesisDataRow row;
        readStream >> row;
        genBlock->addRow(row);
    }
    if (genBlock->getData().isEmpty())
    {
        qWarning().noquote() << QString("Genesis block [%1] is empty").arg(genBlock->toString());
        delete genBlock;
        return nullptr;
    }

    file.remove();
    return genBlock;
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

    int resultCode = fileMode ? blockIndex.addBlock(block) : memIndex.addBlock(block);

    switch (resultCode)
    {
    case 0:
    {
        emit updateLastTransactionList(); // TODO: ?
        qDebug() << "Block" << block.getIndex() << "is successfully added to blockchain";
        break;
    }
    case Errors::FILE_ALREADY_EXISTS:
    {
        qDebug() << "Block" << block.getIndex() << "is already in blockchain";
        if (block.getType() == Config::DATA_BLOCK_TYPE)
        {
            resultCode = mergeBlockWithLocal(block);
        }
        else if (block.getType() == Config::GENESIS_BLOCK_TYPE)
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
            createGenesisBlock();
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
        if (blockA.getType() == Config::DATA_BLOCK_TYPE)
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
        else if (blockA.getType() == Config::GENESIS_BLOCK_TYPE)
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
        Block merged(dataA, &prev);
        signBlock(merged);
        return merged;
    }
    else // Case 2 - different payload
    {
        QList<Transaction> transactionsA = blockA.extractTransactions();
        QList<Transaction> transactionsB = blockB.extractTransactions();

        ListContainer<Transaction> txs;
        txs.addAll(transactionsB);

        for (const Transaction &tx : transactionsA)
        {
            if (!transactionsB.contains(tx))
            {
                txs.add(tx);
            }
        }

        Block mergedBlock(txs.serialize(), &prev);
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
        GenesisBlock merged(dataA, &prev, blockA.getPrevGenHash());
        signBlock(merged);
        return merged;
    }
    else // Case 2 - different payload
    {
        // todo: make utils::combine(list, list) function;
        QList<GenesisDataRow> genDataRowsA = blockA.extractDataRows();
        QList<GenesisDataRow> genDataRowsB = blockB.extractDataRows();

        ListContainer<GenesisDataRow> rows;
        rows.addAll(genDataRowsB);

        for (const GenesisDataRow &r : genDataRowsA)
        {
            if (!genDataRowsB.contains(r))
            {
                rows.add(r);
            }
        }

        GenesisBlock mergedBlock(rows.serialize(), &prev, blockA.getPrevGenHash());
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

void Blockchain::process()
{
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
        qDebug() << QString("Block [%1] already exists in local blockchain").arg(block.getIndex().toString());
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

void Blockchain::getBlockFromBlockchain(SearchEnum::BlockParam param, QByteArray value,
                                        QHostAddress peerAddress, QByteArray requestHash)
{
    Block block = getBlock(param, value);
    if (block.isEmpty())
        return;
    emit BlockFound(block, param, value, peerAddress, requestHash);
}

void Blockchain::getBlockCount(QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "BLOCKCHAIN: getBlockCount() count - " << this->blockIndex.getLastSavedId();
    emit BlockCount(this->blockIndex.getLastSavedId(), peerAddress, requestHash);
}

void Blockchain::getActorCount(QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "BLOCKCHAIN: getActorCount() count - " << this->actorIndex->getLastSavedId();
    emit ActorCount(this->actorIndex->getLastSavedId(), peerAddress, requestHash);
}

void Blockchain::addBlockToBlockchain(Block block)
{
    addBlock(block);
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

void Blockchain::newActor(Actor<KeyPublic> actor)
{
    //    actorIndex->addActor(actor);
}

void Blockchain::getTxFromBlockchain(SearchEnum::TxParam param, QByteArray value, QHostAddress peerAddress,
                                     QByteArray requestHash)
{
    Transaction transaction = getTransaction(param, value);
    if (!transaction.isEmpty())
    {
        emit TxFound(transaction, param, QString(value), peerAddress, requestHash);
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

    if (tx->getSender() == 0)
    {
        bool sig = actorIndex->getActor(0).getKey()->verify(tx->getDataForDigSig(), tx->getDigSig());
        if (sig)
        {
            emit tx->Approved();
            return;
        }
        else
        {
            emit tx->NotApproved();
            qDebug() << "Transaction not approved: false zero user";
            return;
        }
    }

    if (tx->getData() == "genesis")
    {
        // type = 6, token = correct
        Profile profile = actorIndex->getProfile(tx->getSender().toString());

        if (profile.type() != 6)
        {
            emit tx->NotApproved();
            qDebug() << "Transaction not approved: genesis block is not from contract";
            return;
        }

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

    if (tx->getAmount() <= 0)
    {
        emit tx->NotApproved();
        qDebug() << "Transaction not approved: amount <= 0";
        return;
    }

    // verify sender state
    BigNumber targetSender = tx->getSender();
    Transaction senderLastTx = getTxBySenderOrReceiver(targetSender, tx->getToken().toByteArray());
    BigNumber senderCurBal = getBalanceFromTx(targetSender, senderLastTx);
    bool senderBalanceIsValid = false;
    if (tx->getSenderBalance() == senderCurBal)
        senderBalanceIsValid = true;

    // verify receiver state
    BigNumber targetReceiver = tx->getReceiver();
    Transaction receiverLastTx = getTxBySenderOrReceiver(targetReceiver, tx->getToken().toByteArray());
    BigNumber receiverCurBal = getBalanceFromTx(targetReceiver, receiverLastTx);
    bool receiverBalanceIsValid = false;
    if (tx->getReceiverBalance() == receiverCurBal)
        receiverBalanceIsValid = true;

    // qDebug() << "ASDASDASD" << senderCurBal - tx->getAmount();
    if (senderCurBal - tx->getAmount() < 0 /*|| receiverCurBal + tx->getAmount() < 0*/)
    {
        qDebug() << "Transaction not approved: sender's or receiver's balance will be < 0";
        emit tx->NotApproved();
        return;
    }

    if (senderBalanceIsValid && receiverBalanceIsValid)
    {
        emit tx->Approved();
        return;
    }
    else
    {
        emit tx->NotApproved();
        qDebug() << "Transaction not approved: balance not valid";
    }
}

// Transactions //

void Blockchain::getTxPairFromBlockChain(BigNumber sender, BigNumber receiver, QHostAddress peerAddress,
                                         QByteArray requestHash)
{
    // todo
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
