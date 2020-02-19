#include "datastorage/blockchain.h"
#include "managers/tx_manager.h"

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

void Blockchain::setTxManager(TransactionManager *value)
{
    txManager = value;
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

void Blockchain::saveTxInfoInEC(const QByteArray data) const
{
    QList<QByteArray> l = Serialization::universalDeserialize(data, Serialization::TRANSACTION_FIELD_SIZE);
    QList<QByteArray> temp;

    std::vector<DBRow> extractData;
    DBRow resultData;

    QString typeS = "0"; // sender type
    QString typeR = "0"; // receiver type

    DBConnector cacheDB("blockchain/cacheEC.db");
    cacheDB.createTable("CREATE TABLE IF NOT EXISTS cacheData"
                        " ("
                        "ActorId  TEXT   NOT NULL, "
                        "State     TEXT              NOT NULL, "
                        "Token     TEXT              NOT NULL, "
                        "Type   TEXT              NOT NULL );");

    for (auto i : l)
    {
        temp = Serialization::universalDeserialize(i, Serialization::TRANSACTION_FIELD_SIZE);
        if (temp.size() != 12)
        {
            qDebug() << "[Error][" << __FILE__ << __FUNCTION__ << __LINE__ << "]Transaction size !=12";
            return;
        }

        // modify sender data in db
        extractData = cacheDB.select("SELECT State FROM cacheData WHERE ActorId ='" + temp[0].toStdString()
                                     + "' AND Token='" + temp[5].toStdString() + "';");

        resultData["ActorId"] = temp[0].toStdString();
        resultData["Token"] = temp[5].toStdString();
        resultData["Type"] = typeS.toStdString();

        if (extractData.empty())
        {
            resultData["State"] = ('-' + temp[2]).toStdString();
            cacheDB.insert("cacheData", resultData);
        }

        else
        {
            resultData["State"] =
                (BigNumber(QByteArray::fromStdString(extractData[0]["State"])) - BigNumber(temp[2]))
                    .toStdString();
            cacheDB.update("UPDATE cacheData "
                           "SET State ='"
                           + resultData["State"] + "' WHERE ActorId ='" + resultData["ActorId"]
                           + "' AND Token='" + resultData["Token"] + "';");
        }

        extractData.clear();
        resultData.clear();

        // modify receiver data in db
        extractData = cacheDB.select("SELECT State FROM cacheData WHERE ActorId ='" + temp[1].toStdString()
                                     + "' AND Token='" + temp[5].toStdString() + "';");

        resultData["ActorId"] = temp[1].toStdString();
        resultData["Token"] = temp[5].toStdString();
        resultData["Type"] = typeR.toStdString();
        if (extractData.empty())
        {
            resultData["State"] = temp[2].toStdString();
            cacheDB.insert("cacheData", resultData);
        }

        else
        {

            resultData["State"] =
                (BigNumber(QByteArray::fromStdString(extractData[0]["State"])) + BigNumber(temp[2]))
                    .toStdString();
            cacheDB.update("UPDATE cacheData "
                           "SET State ='"
                           + resultData["State"] + "' WHERE ActorId='" + resultData["ActorId"]
                           + "' AND Token='" + resultData["Token"] + "';");
        }
    }
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
        Messages::GetBlockMessage request;
        request.param = SearchEnum::BlockParam::Id;
        request.value = QByteArray::number(0);
        emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
    }
    else
        actorIndex->setCompanyId(new QByteArray(zero.getApprover().toActorId()));
}

BigNumber Blockchain::getSupply(const QByteArray &idToken)
{
    GenesisBlock gen = blockIndex.getLastGenesisBlock();
    BigNumber id = gen.getIndex();
    std::string path = blockIndex.buildFilePath(id).toStdString();
    DBConnector cacheDB(path);
    std::vector<DBRow> extractData =
        cacheDB.select("SELECT * FROM GenesisDataRow WHERE token = '" + idToken.toStdString() + "';");
    BigNumber res = 0;
    for (const auto &tmp : extractData)
    {
        QByteArray sum(tmp.at("state").c_str());
        res += BigNumber(sum).abs();
    }
    return res;
}

BigNumber Blockchain::getFullSupply(const QByteArray &idToken)
{
    BigNumber id = blockIndex.getLastGenesisBlock().getIndex();
    std::string path = blockIndex.buildFilePath(id).toStdString();
    DBConnector cacheDB(path);
    std::vector<DBRow> extractData =
        cacheDB.select("SELECT * FROM GenesisDataRow WHERE token = '" + idToken.toStdString() + "' ;");
    BigNumber res = 0;
    for (const auto &tmp : extractData)
    {
        QByteArray sum(tmp.at("state").c_str());
        res += BigNumber(sum).abs();
    }
    DBConnector cacheDB2("blockchain/cacheEC.db");
    std::vector<DBRow> extractData2 =
        cacheDB2.select("SELECT * FROM cacheData WHERE Token = '" + idToken.toStdString()
                        + "' AND ActorId = '" + actorIndex->companyId->toStdString() + "' ;");
    for (const auto &tmp : extractData2)
    {
        QByteArray sum(tmp.at("State").c_str());
        res += BigNumber(sum).abs();
    }
    return res;
}

Block Blockchain::checkBlock(const Block &block)
{
    if (GenesisBlock::isGenesisBlock(block.serialize()))
        return GenesisBlock(block.serialize());
    else
        return Block(block);
}
// Genesis block //

bool Blockchain::shouldStartGenesisCreation()
{
    return Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS == this->blocksFromLastGenesis;
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
            GenesisDataRow recSender = GenesisDataRow(tx.getSender(), getUserBalance(tx.getSender()),
                                                      tx.getToken(), DataStorage::typeDataRow::UNIVERSAL);
            GenesisDataRow recReceiver = GenesisDataRow(tx.getReceiver(), getUserBalance(tx.getReceiver()),
                                                        tx.getToken(), DataStorage::typeDataRow::UNIVERSAL);
            addRecordsIfNew(recReceiver, recSender);
        }
    }
    return QByteArray();
}

bool Blockchain::signCheckAdd(Block &block)
{
    if (block.getIndex() == 0)
        return false;
    QByteArrayList list = block.getListSignatures();

    int count = 0;
    if (block.getType() == "genesis")
    {
        GenesisBlock saved(getBlockData(SearchEnum::BlockParam::Id, block.getIndex().toByteArray()));

        QByteArrayList savedList = saved.getListSignatures();
        if (!savedList.isEmpty())
        {
            if (list != savedList)
            {
                for (int i = 0; i < list.size(); i += 2)
                {
                    if (!savedList.contains(list[i]))
                    {
                        DBConnector DB;
                        QString path = blockIndex.buildFilePath(block.getIndex());
                        DB.open(path.toStdString());
                        DBRow rowRow;
                        rowRow.insert({ "actorId", list[i].toStdString() });
                        rowRow.insert({ "digSig", list[i + 1].toStdString() });
                        DB.insert(Config::DataStorage::SignTable, rowRow);
                        count++;
                    }
                }
            }
        }
        if (count != 0)
            return true;
        if ((list.size() / 2) > 2)
            return false;

        QByteArray id = accountController->getMainActor()->getId().toByteArray();
        if (!list.contains(id))
        {
            QByteArray sign = accountController->getMainActor()->getKey()->sign(block.getHash());
            block.addSignature(id, sign);
        }
    }
    else
    {
        Block saved(getBlockData(SearchEnum::BlockParam::Id, block.getIndex().toByteArray()));
        QByteArrayList savedList = saved.getListSignatures();
        if (!savedList.isEmpty())
        {
            if (list != savedList)
            {
                for (int i = 0; i < list.size(); i += 2)
                {
                    if (!savedList.contains(list[i]))
                    {
                        DBConnector DB;
                        QString path = blockIndex.buildFilePath(block.getIndex());
                        DB.open(path.toStdString());
                        DBRow rowRow;
                        rowRow.insert({ "actorId", list[i].toStdString() });
                        rowRow.insert({ "digSig", list[i + 1].toStdString() });
                        DB.insert(Config::DataStorage::SignTable, rowRow);
                    }
                }
            }
        }
        if (count != 0)
            return true;
        if ((list.size() / 2) > 2)
            return false;

        QByteArray id = accountController->getMainActor()->getId().toByteArray();
        if (!list.contains(id))
        {
            QByteArray sign = accountController->getMainActor()->getKey()->sign(block.getHash());
            block.addSignature(id, sign);
        }
    }
    return false;
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
                    genBlockData.append(
                        GenesisDataRow(i.key(), i.value(), 0, DataStorage::typeDataRow::UNIVERSAL));
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
            DBConnector cacheDB("blockchain/cacheEC.db");
            std::vector<DBRow> extractData = cacheDB.select("SELECT * FROM cacheData;");
            for (auto i : extractData)
                nb.addRow(
                    GenesisDataRow(BigNumber(QByteArray::fromStdString(i["ActorId"])),
                                   BigNumber(QByteArray::fromStdString(i["State"])),
                                   BigNumber(QByteArray::fromStdString(i["Token"])),
                                   DataStorage::typeDataRow(QByteArray::fromStdString(i["Type"]).toInt())));
            cacheDB.query("DELETE FROM cacheData");
            cacheDB.query("VACUUM");
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

int Blockchain::mergeBlockWithLocal(Block &received)
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
    for (Block &b : tmpBlocks)
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
        for (Block &b : tmpBlocks)
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

int Blockchain::addBlock(Block &block, bool isGenesis)
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
                Messages::GetBlockMessage request;
                request.param = SearchEnum::BlockParam::Id;
                request.value = id.toByteArray();
                emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
            }
        }
    }
    if (block.getIndex() == 0)
    {
        this->actorIndex->setCompanyId(new QByteArray(block.getApprover().toActorId()));
    }
    signCheckAdd(block);
    int resultCode = fileMode ? blockIndex.addBlock(block) : memIndex.addBlock(block);

    switch (resultCode)
    {
    case 0: {
        emit updateLastTransactionList(); // TODO: ?
        qDebug() << "Block" << block.getIndex() << block.getType() << "is successfully added to blockchain";
        getSmContractMembers(block);

        emit sendMessage(block.serialize(), Messages::ChainMessage::blockMessage);
        saveTxInfoInEC(block.getData());
        break;
    }
    case Errors::FILE_ALREADY_EXISTS: {
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
            GenesisBlock gB = createGenesisBlock(*(accountController->getMainActor()));
            if (blockIndex.addBlock(gB) == 0)
            {
                qDebug() << "Block" << gB.getIndex() << gB.getType() << "is successfully added to blockchain";
                emit sendMessage(gB.serialize(), Messages::ChainMessage::genesisBlockMessage);
                blocksFromLastGenesis = 0;
            }
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
    BigNumber balance;

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--)
    {
        Block currentBlock = blockIndex.getBlockById(i);

        if (currentBlock.getData() == "genesis")
        {
            GenesisBlock genesis = blockIndex.getGenesisBlockById(i);
            const auto rows = genesis.extractDataRows();

            for (const auto &row : rows)
            {
                if (userId == row.actorId)
                    return balance + row.state;
            }

            return balance;
        }

        if (currentBlock.isEmpty())
            break;

        QList<Transaction> txs = currentBlock.extractTransactions();

        for (auto &tx : txs)
        {
            if (tx.getSender() == userId && tx.getToken() == tokenId)
            {
                balance -= tx.getAmount();
            }
            else if (tx.getReceiver() == userId && tx.getToken() == tokenId)
            {
                balance += tx.getAmount();
            }
        }
    }

    return balance;
}

void Blockchain::showBlockchain() const
{
    qDebug() << "BLOCKCHAIN: showBlockchain()";
    int i = 0;
    Block currentBlock = blockIndex.getBlockById(i);
    do
    {
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
    emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlockCount);
}

void Blockchain::updateBlockchainForSignIn(QByteArray id, QByteArrayList idList)
{
    Messages::BlockCount request;
    emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlockCount);
}

void Blockchain::checkBlockExistence(Block &block)
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
        Messages::GetBlockMessage request;
        request.param = SearchEnum::BlockParam::Id;
        request.value = count.toByteArray();
        emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
    }
}

void Blockchain::getBlockFromBlockchain(const SearchEnum::BlockParam &param, const QByteArray &value,
                                        const QByteArray &requestHash, const SocketPair &receiver)
{
    QByteArray srBlock = getBlockData(param, value);
    if (srBlock.isEmpty())
        return;
    emit responseReady(srBlock, Messages::GeneralResponse::getBlockResponse, requestHash, receiver);
}

void Blockchain::getBlockCount(const QByteArray &requestHash, const SocketPair &receiver)
{
    qDebug() << "BLOCKCHAIN: getBlockCount() count - " << this->blockIndex.getLastSavedId();

    emit responseReady(this->blockIndex.getLastSavedId().toByteArray(),
                       Messages::GeneralResponse::getBlockCountResponse, requestHash, receiver);
}

void Blockchain::addBlockToBlockchain(Block block)
{
    addBlock(block);
    QList<Transaction> list = block.extractTransactions();
    for (const auto &tmp : list)
    {
        QList<BigNumber> list;
        for (auto tmp : accountController->getAccounts())
            list.append(tmp->getId());

        if (list.contains(tmp.getSender()))
        {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(), notification::NotifyType::TxToUser,
                             tmp.getReceiver().toByteArray() });
        }
        else if (list.contains(tmp.getReceiver()))
        {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(), notification::NotifyType::TxToMe,
                             tmp.getSender().toByteArray() });
        }
    }
}

void Blockchain::addGenBlockToBlockchain(GenesisBlock block)
{
    if (block.getIndex() == 0)
    {
        mutex.lock();
        this->actorIndex->setCompanyId(new QByteArray(block.getApprover().toActorId()));
        mutex.unlock();
    }
    if (blockIndex.addBlock(block) == 0 || signCheckAdd(block))
        sendMessage(block.serialize(), Messages::ChainMessage::genesisBlockMessage);
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
        emit responseReady(transaction.serialize(), Messages::GeneralResponse::getTxResponse, request,
                           receiver);
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
    if (tx->getData() == "initcontract")
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
    if (targetSender.toActorId() != *actorIndex->companyId)
    {
        senderCurBal = getUserBalance(targetSender, tx->getToken());
        BigNumber check = txManager->checkPendingTxsList(targetSender);
        senderCurBal += check;
    }

    if (senderCurBal - tx->getAmount() < 0 && targetSender.toActorId() != *actorIndex->companyId)
    {
        qDebug() << senderCurBal << tx->getAmount();
        qDebug() << "Transaction "
                    "not approved: sender's or receiver's balance will be < 0";
        emit tx->NotApproved();
        return;
    }

    tx->sign(accountController->getCurrentActor());
    emit tx->Approved();
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
