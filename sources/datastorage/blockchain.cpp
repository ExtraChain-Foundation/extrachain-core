/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <QJsonObject>

#include "datastorage/blockchain.h"
#include "datastorage/dfs/dfs_controller.h"
#include "datastorage/index/actorindex.h"
#include "managers/data_mining_manager.h"
#include "managers/tx_manager.h"

#undef qCritical // temp
#define qCritical qDebug

Blockchain::Blockchain(ExtraChainNode *node) {
    this->node = node;
    genBlockData.clear();

    //    setCirculativeSupply(blockIndex.calculateCirculativeBalance());
    //    increaseCirculativeSupply(blockIndex.calculateCirculativeBalanceLastGenesisBlock());
}

Blockchain::~Blockchain() {
}

BlockVariant Blockchain::getBlockByIndex(const BigNumber &index, const bool makeRequestBlock) {
    BlockVariant block = blockIndex.getBlockById(index);
    if (block.isEmpty() && index >= 0 && makeRequestBlock) {
        std::pair<BlockType, BigNumber> requestData(BlockType::Data, index);
        node->network()->send_message(requestData, MessageType::BlockchainRequestBlock);
    }
    return block;
}

BigNumber Blockchain::checkIntegrity() {
    // check in FileIndex: start from second block
    for (BigNumber i = 1; i < blockIndex.getRecords(); i++) {
        BlockVariant prev = blockIndex.getBlockByPosition(i - 1);
        BlockVariant cur  = blockIndex.getBlockByPosition(i);
        if (cur.getPrevHash() != prev.getHash()) {
            return cur.getIndex();
        }
    }

    return BigNumber();
}

void Blockchain::setTxManager(TransactionManager *value) {
    txManager = value;
}

// Blocks //

BlockVariant Blockchain::getLastBlock() const {
    BlockVariant block = blockIndex.getLastBlock();
    return validateAndReturnBlock(block);
}

BigNumber Blockchain::getBlocksStored() const {
    return blockIndex.getLastSavedId() - blockIndex.getFirstSavedId() + 1;
}

BlockVariant Blockchain::getLastRealBlock() const {
    BlockVariant block = blockIndex.getLastRealBlock();
    return validateAndReturnBlock(block);
}

BlockVariant Blockchain::getBlockByData(const QByteArray &data) {
    BlockVariant block = blockIndex.getBlockByData(data);
    return validateAndReturnBlock(block);
}

BlockVariant Blockchain::getBlockByHash(const QByteArray &hash) {
    BlockVariant block = blockIndex.getBlockByHash(hash);
    return validateAndReturnBlock(block);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByHash(const QByteArray &hash, const QByteArray &token) {
    return blockIndex.getLastTxByHash(hash, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxBySender(const BigNumber &id, const QByteArray &token) {
    return blockIndex.getLastTxBySender(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByReceiver(const BigNumber &id, const QByteArray &token) {
    return blockIndex.getLastTxByReceiver(id, token);
}

std::pair<Transaction, QByteArray>
Blockchain::getTxBySenderOrReceiver(const BigNumber &id, const QByteArray &token) {
    return blockIndex.getLastTxBySenderOrReceiver(id, token);
}

std::pair<Transaction, QByteArray>
Blockchain::getTxBySenderOrReceiverAndToken(const BigNumber &id, const QByteArray &token) {
    return blockIndex.getLastTxBySenderOrReceiverAndToken(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByApprover(const BigNumber &id, const QByteArray &token) {
    return blockIndex.getLastTxByApprover(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByUser(const BigNumber &id, const QByteArray &token) {
    return blockIndex.getLastTxByApprover(id, token);
}

void Blockchain::saveTxInfoInEC(const std::set<Transaction> &transactions) const {
    std::vector<DBRow> extractData;
    DBRow              resultData;

    QString typeS = "0"; // sender type
    QString typeR = "0"; // receiver type

    DBConnector cacheDB("blockchain/cacheEC.db");
    cacheDB.open();
    cacheDB.createTable(
        "CREATE TABLE IF NOT EXISTS cacheData"
        " ("
        "ActorId   TEXT     NOT NULL, "
        "State     TEXT     NOT NULL, "
        "Token     TEXT     NOT NULL, "
        "Type      TEXT     NOT NULL );");

    for (const auto &q : transactions) {
        // modify sender data in db
        extractData = cacheDB.select(fmt::format(
            "SELECT State FROM cacheData WHERE ActorId ='{}' AND Token='{}';",
            q.getSender().toStdString(),
            q.getToken().toStdString()));
        cacheDB.select(fmt::format(
            "SELECT State FROM cacheData WHERE ActorId ='{}' AND Token='{}';",
            q.getSender().toStdString(),
            q.getToken().toStdString()));

        resultData["ActorId"] = q.getSender().toStdString();
        resultData["Token"]   = q.getToken().toStdString();
        resultData["Type"]    = typeS.toStdString();

        if (extractData.empty()) {
            resultData["State"] = '-' + q.getAmount().toStdString();
            cacheDB.insert("cacheData", resultData);
        }

        else {
            resultData["State"] = (BigNumberFloat(extractData[0]["State"]) - q.getAmount()).toStdString();
            cacheDB.update(fmt::format(
                "UPDATE cacheData SET State ='{}' WHERE ActorId ='{}' AND Token='{}';",
                resultData["State"],
                resultData["ActorId"],
                resultData["Token"]));
        }

        extractData.clear();
        resultData.clear();

        // modify receiver data in db
        extractData = cacheDB.select(fmt::format(
            "SELECT State FROM cacheData WHERE ActorId ='{}' AND Token='{}';",
            q.getReceiver().toStdString(),
            q.getToken().toStdString()));

        resultData["ActorId"] = q.getReceiver().toStdString();
        resultData["Token"]   = q.getToken().toStdString();
        resultData["Type"]    = typeR.toStdString();
        if (extractData.empty()) {
            resultData["State"] = q.getAmount().toStdString();
            cacheDB.insert("cacheData", resultData);
        } else {
            resultData["State"] = (BigNumberFloat(extractData[0]["State"]) + q.getAmount()).toStdString();
            cacheDB.update(fmt::format(
                "UPDATE cacheData SET State ='{}' WHERE ActorId='{}' AND Token='{}';",
                resultData["State"],
                resultData["ActorId"],
                resultData["Token"]));
        }
    }
}

std::set<Transaction>
Blockchain::getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from, int count, BigNumber token) {
    return blockIndex.getTxsBySenderOrReceiverInRow(id, from, count, token);
}

void Blockchain::getBlockZero() {
    BlockVariant zero = getBlockByIndex(0, true);
    if (zero.isEmpty()) {
        // TODONEW
        // Messages::GetBlockMessage request;
        // request.param = SearchEnum::BlockParam::Id;
        // request.value = QByteArray::number(0);
        // emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
    } else {
        updateFirstId(zero);
    }
}

BigNumber Blockchain::getSupply(const QByteArray &idToken) {
    GenesisBlock gen  = blockIndex.getLastGenesisBlock();
    BigNumber    id   = gen.getIndex();
    std::string  path = blockIndex.buildFilePath(id).toStdString();
    DBConnector  cacheDB(path);
    cacheDB.open();
    std::vector<DBRow> extractData = cacheDB.select(
        fmt::format("SELECT * FROM GenesisDataRow WHERE token = '{}';", idToken.toStdString()));
    BigNumber res = 0;
    for (const auto &tmp : extractData) {
        res += BigNumber(tmp.at("state")).abs();
    }
    return res;
}

BigNumber Blockchain::getFullSupply(const QByteArray &idToken) {
    BigNumber   id   = blockIndex.getLastGenesisBlock().getIndex();
    std::string path = blockIndex.buildFilePath(id).toStdString();
    DBConnector cacheDB(path);
    cacheDB.open();
    std::vector<DBRow> extractData = cacheDB.select(
        fmt::format("SELECT * FROM GenesisDataRow WHERE token = '{}';", idToken.toStdString()));
    BigNumber res = 0;
    for (const auto &tmp : extractData) {
        res += BigNumber(tmp.at("state")).abs();
    }
    DBConnector cacheDB2("blockchain/cacheEC.db");
    cacheDB2.open();
    std::vector<DBRow> extractData2 =
        cacheDB2.select(fmt::format("SELECT * FROM cacheData WHERE Token = '{}';", idToken.toStdString()));
    for (const auto &tmp : extractData2) {
        std::string sum = tmp.at("State");
        if (sum[0] == '-')
            continue;
        res += BigNumber(sum).abs();
    }
    return res;
}

void Blockchain::sendBlockByNumber(const BigNumber &index) const {
    BlockVariant answerBlock = blockIndex.getBlockById(index);
    std::string  data;
    if (answerBlock.getType() == BlockType::Genesis) {
        GenesisBlock genesisBlock = blockIndex.getGenesisBlockById(index);
        node->network()->send_message(genesisBlock, MessageType::BlockchainGenesisBlock);
    } else {
        auto dataBlock = answerBlock.getBlockConst();
        node->network()->send_message(dataBlock, MessageType::BlockchainNewBlock);
    }
}

void Blockchain::sendLastGenesisBlock() const {
    const GenesisBlock genesisBlock = blockIndex.getLastGenesisBlock();
    node->network()->send_message(genesisBlock, MessageType::BlockchainGenesisBlock);
}

// Genesis block //

bool Blockchain::shouldStartGenesisCreation() {
    return Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS == this->blocksFromLastGenesis;
}

void Blockchain::addRecordsIfNew(const GenesisDataRow &row1, const GenesisDataRow &row2) {
    bool b1 = false;
    bool b2 = false;
    for (int i = 0; i < genBlockData.size(); i++) {
        if (genBlockData[i].actorId == row1.actorId && genBlockData[i].token == row1.token) {
            b1 = true;
        }
        if (genBlockData[i].actorId == row2.actorId && genBlockData[i].token == row2.token) {
            b2 = true;
        }
        if (b1 && b2) {
            return;
        } else {
            if (!b1) {
                genBlockData.push_back(row1);
            }
            if (!b2) {
                genBlockData.push_back(row2);
            }
            return;
        }
    }
}

QByteArray Blockchain::findRecordsInBlock(const BlockVariant &block) {
    if (block.getType() == BlockType::Genesis) {
        return QByteArray::fromStdString(block.getHash());
    } else if (!block.isEmpty()) {
        const auto transactions = block.transactions();
        for (const Transaction &tx : std::as_const(transactions)) {
            if (tx.getReceiver() == node->actorIndex()->firstId())
                break;
            GenesisDataRow recSender = GenesisDataRow(
                tx.getSender(),
                getUserBalance(tx.getSender(), tx.getToken()),
                tx.getToken(),
                DataStorage::DataRowType::Universal);
            GenesisDataRow recReceiver = GenesisDataRow(
                tx.getReceiver(),
                getUserBalance(tx.getReceiver(), tx.getToken()),
                tx.getToken(),
                DataStorage::DataRowType::Universal);
            addRecordsIfNew(recReceiver, recSender);
        }
    }
    return QByteArray();
}

GenesisBlock Blockchain::createGenesisBlock(
    const std::shared_ptr<Actor<KeyPrivate>> actor,
    QMap<ActorId, BigNumberFloat>            states) {
    qDebug() << "Creating genesis block";
    genBlockData.clear();
    // QByteArray previousGenHash;
    GenesisBlock nb;
    nb.setIndex(0);

    if (blockIndex.getLastSavedId().isEmpty()) {
        qCritical() << "Can't create genesis block, there no last saved id";
        return nb;
    }
    if (blockIndex.getRecords() == 0) {
        if (blockIndex.getFirstSavedId() == 0 && blockIndex.getLastSavedId() == 0) {
            for (auto i = states.begin(); i != states.end(); i++) {
                genBlockData.push_back(
                    GenesisDataRow(i.key(), i.value(), ActorId(), DataStorage::DataRowType::Universal));
            }

            nb.addData(actor->id().toStdString());
            nb.addRows(genBlockData);

            // nb.setApprover(BigNumber(*(actorIndex->m_firstId)));
            nb.sign(node->accountController()->currentProfile().getActor(node->actorIndex()->firstId()));
        } else
            qCritical() << "Can't create genesis block, there no blocks in blockIndex";
        return nb;
    } else {
        BlockVariant prevGen = BlockVariant(Block());
        auto         b       = BlockVariant(Block());
        BigNumber    i       = blockIndex.getLastSavedId();
        nb                   = GenesisBlock();
        nb.setPrev(blockIndex.getBlockById(blockIndex.getLastSavedId()));
        while ((blockIndex.getBlockById(i).getType() != BlockType::Genesis)
               && (i >= blockIndex.getFirstSavedId())) {
            b = blockIndex.getBlockById(i);
            findRecordsInBlock(b);
            i--;
            prevGen = blockIndex.getBlockById(i);
        }

        // TODO: genBlockData?
        if (!prevGen.isEmpty()) {
            auto dataRows = prevGen.getGenesisBlockConst()->get().dataRows();
            nb.addRows(dataRows);
        }

        DBConnector cacheDB("blockchain/cacheEC.db");
        cacheDB.open();
        std::vector<DBRow> extractData = cacheDB.select("SELECT * FROM cacheData;");
        for (auto i : extractData)
            nb.addRow(GenesisDataRow(
                i["ActorId"],
                BigNumber(i["State"]),
                i["Token"],
                DataStorage::DataRowType(QByteArray::fromStdString(i["Type"]).toInt())));
        cacheDB.query("DELETE FROM cacheData");
        cacheDB.query("VACUUM");
        nb.setPrevGenHash(prevGen.getHash()); // (blockIndex.getBlockById(i).getHash());
    }
    qDebug() << "Genesis block created";
    genBlockData.clear();
    nb.sign(actor);
    return nb;
}

// Merging //

int Blockchain::mergeBlockWithLocal(BlockVariant &received) {
    const auto   receivedBlockIndex = received.getIndex();
    BlockVariant existed            = getBlockByIndex(receivedBlockIndex);
    if (!canMergeBlocks(received, existed)) {
        qWarning() << "Blocks with id" << receivedBlockIndex << "can't be merged";
        return Errors::BLOCKS_CANT_MERGE;
    }

    qDebug() << "Start merging block" << receivedBlockIndex;
    if (received == existed) {
        qDebug() << QString("Blocks are equal ([%1])").arg(Errors::BLOCKS_ARE_EQUAL);
        return Errors::BLOCKS_ARE_EQUAL;
    }
    //    if (received.contain(existed)) // hui znaet nahuya ono
    //    {
    //        removeBlock(existed);
    //        int res = addBlock(received);
    //        return res;
    //    }

    // step 1 - create merged block
    if (received.isGenesisBlock() || existed.isGenesisBlock()) {
        qFatal("Please, check, error if try incorrect genesis merge");
        return Errors::BLOCKS_CANT_MERGE;
    }

    Block merged = mergeBlocks(received.getAny(), existed.getAny());

    if (merged.isEmpty())
        return Errors::BLOCKS_CANT_MERGE;

    // step 2 - collect all blocks from old to latest
    QList<BlockVariant> tmpBlocks; // from existed to last block;

    const auto lastBlockIndex = getLastBlock().getIndex();
    // only if indexes is different
    if (receivedBlockIndex != lastBlockIndex) {
        // we should collect temp blocks
        BigNumber lastBlockId = existed.getIndex();
        BigNumber nextBlockId = lastBlockIndex;
        for (BigNumber i = lastBlockId; i <= nextBlockId; i++) {
            tmpBlocks << getBlockByIndex(i);
        }
        if (tmpBlocks.isEmpty()) {
            qWarning() << "Error: There is no blocks found locally while merging block" << receivedBlockIndex;
            return Errors::NO_BLOCKS;
        }
    }

    // step 3 - update hash, prevHash and approver for all modified blocks
    std::string newHash = merged.getHash();
    std::string oldHash = existed.getHash();
    for (BlockVariant &b : tmpBlocks) {
        if (b.getPrevHash() == oldHash) {
            oldHash = b.getHash();
            b.setPrevHash(newHash);
            b.setType(BlockType::DataMerge);
            signBlock(b);
            newHash = b.getHash();
        }
    }

    // step 4 - remove existed block (and all blocks after them)
    // and save updated blocks with new hash
    removeBlock(existed);
    auto mergedVariant = BlockVariant(merged);
    addBlock(mergedVariant);
    for (BlockVariant &b : tmpBlocks) {
        addBlock(b);
    }
    //  emit SendMergedBlock(existed, received, merged);
    return 0;
}

int Blockchain::mergeGenesisBlockWithLocal(const GenesisBlock &received) {
    const auto   receivedIndex = received.getIndex();
    GenesisBlock existed       = blockIndex.getGenesisBlockById(receivedIndex);
    if (!existed.isEmpty()) {
        // saved block with the same id is genesis
        qDebug() << QString("Start merging genesis block [%1] with exising [%2]")
                        .arg(received.toString(), existed.toString());

        // step 1
        GenesisBlock merged = mergeGenesisBlocks(received, existed);

        // step 2 - collect all blocks from old to latest
        QList<BlockVariant> tmpBlocks; // from existed to last block;

        const auto lastBlockIndex = getLastBlock().getIndex();
        // only if indexes is different
        if (receivedIndex != lastBlockIndex) {
            // we should collect temp blocks
            BigNumber lastBlockId = existed.getIndex();
            BigNumber nextBlockId = lastBlockIndex;
            for (BigNumber i = lastBlockId; i <= nextBlockId; i++) {
                tmpBlocks << getBlockByIndex(i);
            }
            if (tmpBlocks.isEmpty()) {
                qWarning() << "Error: There is no blocks found locally while merging block" << receivedIndex;
                return Errors::NO_BLOCKS;
            }
        }

        // step 3 - update hash, prevHash and approver for all modified blocks
        std::string newHash = merged.getHash();
        std::string oldHash = existed.getHash();
        for (BlockVariant &b : tmpBlocks) {
            if (b.getPrevHash() == oldHash) {
                oldHash = b.getHash();
                b.setPrevHash(newHash);
                b.setType(BlockType::GenesisMerge);
                signBlock(b);
                newHash = b.getHash();
            }
        }

        // step 4 - remove existed block (and all blocks after them)
        // and save updated blocks with new hash
        removeBlock(BlockVariant(existed));
        auto mergedVariant = BlockVariant(merged);
        addBlock(mergedVariant, true);
        for (BlockVariant &b : tmpBlocks) {
            addBlock(b);
        }
    } else {
        qCritical() << "Can't find genesis block with id=" << receivedIndex << "locally";
        return Errors::NO_BLOCKS;
    }
    return 0;
}

BlockVariant Blockchain::getBlock(SearchEnum::BlockParam type, const QByteArray &value) {
    BlockVariant res = BlockVariant(Block()); // TODO: expected
    switch (type) {
    case SearchEnum::BlockParam::Id:
        res = getBlockByIndex(BigNumber(value.toStdString()));
        break;
    case SearchEnum::BlockParam::Data:
        res = getBlockByData(value);
        break;
    case SearchEnum::BlockParam::Hash:
        res = getBlockByHash(value);
        break;
    default:
        res = BlockVariant(Block());
        break;
    }
    return res;
}

std::pair<Transaction, QByteArray>
Blockchain::getTransaction(SearchEnum::TxParam type, const QByteArray &value, const QByteArray &token) {
    switch (type) {
    case SearchEnum::TxParam::UserSenderOrReceiverOrToken:
        return getTxBySenderOrReceiverAndToken(value.toStdString(), token);
    case SearchEnum::TxParam::Hash:
        return getTxByHash(value, token);
    case SearchEnum::TxParam::User:
        return getTxByUser(value.toStdString(), token);
    case SearchEnum::TxParam::UserApprover:
        return getTxByApprover(value.toStdString(), token);
    case SearchEnum::TxParam::UserReceiver:
        return getTxByReceiver(value.toStdString(), token);
    case SearchEnum::TxParam::UserSender:
        return getTxBySender(value.toStdString(), token);
    case SearchEnum::TxParam::UserSenderOrReceiver:
        return getTxBySenderOrReceiver(value.toStdString(), token);
    default:
        qWarning() << "Can't get tx: incorrent SearchEnum::TxParam. Value:" << value;
        return { Transaction(), "-1" };
    }
}

bool Blockchain::validateBlock(const BlockVariant &block) {
    return node->actorIndex()->validateBlock(block);
}

BlockVariant Blockchain::validateAndReturnBlock(const BlockVariant &block) const {
    // Get prev block hash and check if it exists in current one :)
    return block;
}

BigNumberFloat Blockchain::calculateRewardAmount() const {
    //(dataStoredSize/dfsSize + bytesReceived/BytesSent)+(blocksStoredSize/blockchainSize) * k (k=100)
    const auto &totalBytes = node->network()->getCalculateTraffic()->totalBytes();

    if (totalBytes.first == 0 || node->dfs()->totalDfsSize() == 0) {
        qDebug() << "[Blockchain] Cannot calculate reward due to division by zero. TotalBytes, total dfs:"
                 << totalBytes.first << node->dfs()->totalDfsSize();
        return 0;
    }

    return (
        BigNumberFloat { node->dfs()->sizeTaken() } / node->dfs()->totalDfsSize()
        + BigNumberFloat { totalBytes.second } / totalBytes.first
        + (BigNumberFloat { getBlocksStored() } / getLastBlock().getIndex() * 100));
}

BigNumberFloat Blockchain::calculateRewardAmount(const DFS::Reward::RequestReward &requestReward) const {
    if (requestReward.BytesSent == 0 || node->dfs()->totalDfsSize() == 0) {
        qDebug() << "[Blockchain] Cannot calculate reward due to division by zero. BytesSent, total dfs:"
                 << requestReward.BytesSent << node->dfs()->totalDfsSize();
        return 0;
    }

    return (
        BigNumberFloat { requestReward.DataStoredSize } / node->dfs()->totalDfsSize()
        + BigNumberFloat { requestReward.BytesReceived } / requestReward.BytesSent
        + (BigNumberFloat { requestReward.BlocksStored } / getLastBlock().getIndex() * 100));
}

void Blockchain::updateFirstId(const BlockVariant &block) {
    if (!block.isGenesisBlock() || block.getIndex() != 0)
        return;

    if (block.dataService().size() > 1 || block.dataService().empty())
        qFatal("Incorrect first genesis");

    auto firstId = ActorId(*block.dataService().begin());
    if (!firstId.isEmpty())
        node->actorIndex()->setFirstId(firstId);
}

int Blockchain::addBlock(BlockVariant &block, bool isGenesis) {
    if (block.getType() == BlockType::Genesis) {
        qDebug() << "[Blockchain] Adding a genesis block" << block.getIndex() << "to storage";
    } else {
        qDebug() << "[Blockchain] Adding a block" << block.getIndex() << "to storage";
    }

    const auto indexBlock = block.getIndex();
    if (!(block.getType() == BlockType::Genesis)) {
        if (indexBlock != 0) {
            BigNumber id = block.getIndex() - 1;
            if (getBlock(SearchEnum::BlockParam::Id, id.toByteArray()).isEmpty()) {
                // TODONEW
                // Messages::GetBlockMessage request;
                // request.param = SearchEnum::BlockParam::Id;
                // request.value = id.toByteArray();
                // emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
            }
        }
    }

    this->updateFirstId(block);

    if (indexBlock < 0) {
        qFatal("Add block: index < 0");
        return Errors::BLOCK_IS_NOT_VALID;
    }

    int        resultCode = blockIndex.addBlock(block);
    const auto blockType  = block.getType();

    switch (resultCode) {
    case 0: {
        emit updateLastTransactionList(); // TODO: ?
        qDebug() << "[Blockchain] Block" << indexBlock << "is successfully added to blockchain";
        getSmContractMembers(block);

        // TODONEW emit sendMessage(block.serialize(), Messages::ChainMessage::BlockMessage);
        if (blockType == BlockType::Data) {
            saveTxInfoInEC(block.transactions());
        }
        // qDebug() << (blockType == BlockType::Data) << blockType;
        node->dataMiningManager()->coinRewardRequest(indexBlock);

        break;
    }
    case Errors::FILE_ALREADY_EXISTS: {
        qDebug() << "[Blockchain] Block" << indexBlock << blockType << "is already in blockchain";
        if (blockType == BlockType::Data || blockType == BlockType::DataMerge
            || blockType == BlockType::Dummy) {
            resultCode = mergeBlockWithLocal(block);
        } else if ((blockType == BlockType::Genesis) || (block.getType() == BlockType::GenesisMerge)) {
            resultCode = mergeGenesisBlockWithLocal(*block.getGenesisBlockConst());
        } else {
            qCritical() << "Unsupported block type in block: " << block.getIndex();
        }
        break;
    }
    default:
        qCritical() << "[Blockchain] While adding a new block" << block.toString();
    }

    if (indexBlock % 20 == 0) {
        const auto &actor      = node->accountController()->mainActor();
        const auto &totalBytes = node->network()->getCalculateTraffic()->totalBytes();
        requestCoins({ .Actor              = actor->id().toStdString(),
                       .DataStoredSize     = node->dfs()->sizeTaken(),
                       .TypeFunctioningObj = DFS::Reward::Base,
                       .RewardAmount       = calculateRewardAmount(),
                       .BytesSent          = totalBytes.first,
                       .BytesReceived      = totalBytes.second,
                       .BlocksStored       = getBlocksStored() });
    }

    // after adding genesis block we don't need to increment counter
    if (!isGenesis && resultCode == 0) {
        blocksFromLastGenesis++;
        if (shouldStartGenesisCreation()) {
            const auto &actor = node->accountController()->mainActor();

            GenesisBlock gB = createGenesisBlock(actor);
            if (blockIndex.addBlock(BlockVariant(gB)) == 0) {
                qDebug() << "[Blockchain] Block" << gB.getIndex() << gB.getType()
                         << "is successfully added to blockchain";
                // TODONEW emit sendMessage(gB.serialize(),
                // Messages::ChainMessage::GenesisBlockMessage);
                blocksFromLastGenesis = 0;
            }
        }
    }

    return resultCode;
}

int Blockchain::removeBlock(const BlockVariant &block) {
    return blockIndex.removeById(block);
}

void Blockchain::removeAllDummyBlocks(const BlockVariant &block) {
    blockIndex.removeDummyBlocks(block.getIndex());
}

bool Blockchain::canMergeBlocks(const BlockVariant &receivedBlock, const BlockVariant &existedBlock) {
    // 1) Blocks are approved
    // 2) Blocks has one type
    // 3) Blocks ids are identical
    if (!receivedBlock.getSignature().empty() && !existedBlock.getSignature().empty()
        && receivedBlock.getType() == existedBlock.getType()
        && receivedBlock.getIndex() == existedBlock.getIndex()) {
        if ((receivedBlock.getType() == BlockType::Data) || (receivedBlock.getType() == BlockType::Genesis)
            || (receivedBlock.getType() == BlockType::Dummy))
            return true;
        else if (receivedBlock.getType() == BlockType::GenesisMerge) {
            // 4) at least one common data row
            // TODO: need get?
            std::set<GenesisDataRow> rowsA = receivedBlock.getGenesisBlockConst()->get().dataRows();
            std::set<GenesisDataRow> rowsB = existedBlock.getGenesisBlockConst()->get().dataRows();
            for (const GenesisDataRow &g : rowsA) {
                if (rowsB.contains(g)) {
                    return true;
                }
            }
        } else if (receivedBlock.getType() == BlockType::DataMerge) {
            // 4) at least one common transaction
            auto transactionsA = receivedBlock.transactions();
            auto transactionsB = existedBlock.transactions();
            for (const Transaction &tr : transactionsA) {
                if (transactionsB.contains(tr)) {
                    return true;
                }
            }
        }
    }
    return false;
}

Block Blockchain::mergeBlocks(const Block &blockA, const Block &blockB) {
    qDebug() << "[Blockchain] Attempting to merge" << blockA << "and" << blockB;

    if (blockA.getIndex() == BigNumber(0))
        return Block();

    BlockVariant prev = getBlockByIndex(blockA.getIndex() - 1);
    if (prev.isEmpty()) {
        qWarning() << "[Blockchain] Can't merge" << blockA << "with" << blockB << " - there no prev block";
        return Block();
    }

    const auto &dataServiceA  = blockA.dataService();
    const auto &dataServiceB  = blockB.dataService();
    const auto &transactionsA = blockA.transactions();
    const auto &transactionsB = blockB.transactions();
    const auto &signaturesA   = blockA.signatures();
    const auto &signaturesB   = blockB.signatures();

    bool isDataServiceEqual  = dataServiceA == dataServiceB;
    bool isTransactionsEqual = transactionsA == transactionsB;

    // Case 1 - equal payload
    if (isDataServiceEqual && isTransactionsEqual) {
        Block merged = Block(blockA);
        merged.setPrev(prev);

        if (signaturesA != signaturesB) {
            merged.addSignatures(signaturesB);
        }

        BlockVariant mergedVariant = BlockVariant(merged);
        signBlock(mergedVariant);
        return *mergedVariant.getBlockConst();
    } else { // Case 2 - different payload
        Block merged = Block(blockA);
        merged.clearSignatures();
        if (!isDataServiceEqual)
            merged.addDatas(dataServiceB);
        if (!isTransactionsEqual)
            merged.addTransactions(transactionsB);
        merged.setPrev(prev);
        BlockVariant mergedVariant = BlockVariant(merged);
        signBlock(mergedVariant);
        return *mergedVariant.getBlockConst();
    }
}

GenesisBlock Blockchain::mergeGenesisBlocks(const GenesisBlock &blockA, const GenesisBlock &blockB) {
    qDebug() << "[Blockchain] Attempting to merge" << blockA << "and" << blockB;

    BlockVariant prev = getBlockByIndex(blockA.getIndex() - 1);
    if (prev.isEmpty()) {
        qWarning() << "[Blockchain] Can't merge" << blockA << "with" << blockB << " - there no prev block";
        return GenesisBlock();
    }

    const auto &dataServiceA = blockA.dataService();
    const auto &dataServiceB = blockB.dataService();
    const auto &dataRowsA    = blockA.dataRows();
    const auto &dataRowsB    = blockB.dataRows();
    const auto &signaturesA  = blockA.signatures();
    const auto &signaturesB  = blockB.signatures();

    bool isDataServiceEqual = dataServiceA == dataServiceB;
    bool isDataRowsEqual    = dataRowsA == dataRowsB;

    // Case 1 - equal payload
    if (isDataServiceEqual && isDataRowsEqual) {
        GenesisBlock merged(blockA);
        merged.setPrevGenHash(blockA.getPrevGenHash());
        merged.setPrev(prev);

        if (signaturesA != signaturesB) {
            merged.addSignatures(signaturesB);
        }

        auto mergedVariant = BlockVariant(merged);
        signBlock(mergedVariant);
        return *mergedVariant.getGenesisBlockConst();
    } else { // Case 2 - different payload
        GenesisBlock merged = GenesisBlock(blockA);
        merged.clearSignatures();

        if (!isDataRowsEqual) {
            int count = merged.addRows(dataRowsB);
            if (count < Config::NECESSARY_SAME_TX) {
                qFatal("[Blockchain] Need to test count");
                return GenesisBlock();
            }
        }

        if (!isDataServiceEqual)
            merged.addDatas(dataServiceB);
        merged.setPrev(prev);

        BlockVariant mergedVariant = BlockVariant(merged);
        signBlock(mergedVariant);
        return *mergedVariant.getGenesisBlockConst();
    }
}

void Blockchain::signBlock(BlockVariant &block) const {
    block.sign(node->accountController()->currentWallet());
}

BigNumber Blockchain::getRecords() const {
    return blockIndex.getRecords();
}

BigNumber Blockchain::getCountRealBlockRecords() const {
    return blockIndex.getCountRealBlocks();
}

int Blockchain::getCountTransactionsInBlocks() const {
    return blockIndex.getCountTransactionsInBlocks();
}

BigNumberFloat Blockchain::getUserBalance(ActorId userId, ActorId tokenId, TypeTx typeTx) const {
    BigNumberFloat balance;

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--) {
        BlockVariant currentBlock = blockIndex.getBlockById(i);

        if (currentBlock.getType() == BlockType::Genesis && currentBlock.isGenesisBlock()) {
            GenesisBlock genesis = blockIndex.getGenesisBlockById(i);
            const auto   rows    = genesis.dataRows();

            for (const auto &row : rows) {
                if (userId == row.actorId)
                    balance += row.state;
            }

            return balance;
        }

        if (currentBlock.isEmpty())
            break;

        auto txs = currentBlock.transactions();
        for (auto &tx : txs) {
            if (tx.getTypeTx() != typeTx)
                continue;

            if (tx.getReceiver() == userId && tx.getToken() == tokenId) {
                balance += tx.getAmount();
            }

            if (tx.getSender() == userId) {
                balance -= tx.getAmount();
            }
        }
    }

    return balance;
}

void Blockchain::showBlockchain() const {
    qDebug() << "[Blockchain] Show blockchain:";

    GenesisBlock genBlock = blockIndex.getLastGenesisBlock();
    qDebug() << "Last genesis:" << genBlock;

    int          i            = 0;
    BlockVariant currentBlock = blockIndex.getBlockById(i);
    do {
        i++;
        currentBlock = blockIndex.getBlockById(i);
        qDebug() << currentBlock.getAny();
    } while (!currentBlock.isEmpty());
}

void Blockchain::getSmContractMembers(const BlockVariant &block) const {
    if (block.isGenesisBlock())
        return;
    auto txList = block.transactions();
    for (const Transaction &tx : txList) {
        if (tx.getData() == "InitContract") {
            node->actorIndex()->getActor(tx.getSender());
            node->actorIndex()->getActor(tx.getReceiver());
        }
    }
}

BigNumber Blockchain::getCirculativeSuply() const {
    return circulativeSupply;
}

void Blockchain::setCirculativeSupply(const BigNumber &newValue) {
    circulativeSupply = newValue;
}

void Blockchain::increaseCirculativeSupply(const BigNumber &value) {
    circulativeSupply += value;
    setPossibleMining(circulativeSupply <= Config::ExtraCoin::totalSupply);
}

void Blockchain::requestCoins(const DFS::Reward::RequestReward &requestReward) {
    node->network()->send_message(requestReward, MessageType::BlockchainCoinReward, MessageStatus::Request);
}

void Blockchain::sendCoinsReward(const DFS::Reward::RequestReward &requestReward) {
    if ((calculateRewardAmount(requestReward) - requestReward.RewardAmount) <= 100) {
        Transaction transaction;
        transaction.setSender(node->accountController()->mainActor()->id());
        transaction.setReceiver(requestReward.Actor);
        transaction.setAmount(requestReward.RewardAmount);
        transaction.setDate(QDateTime::currentMSecsSinceEpoch());
        transaction.setTypeTx(TypeTx::RewardTransaction);
        node->network()->send_message(transaction, MessageType::BlockchainTransaction);
    }
}

void Blockchain::setPossibleMining(const bool &value) {
    if (value != possibleMining) {
        emit possibleMiningChange(value);
    }
    possibleMining = value;
}

bool Blockchain::getPossibleMining() const {
    return possibleMining;
}

BigNumber Blockchain::getBlockIndexLastFarmingTx() const {
    return blockIndex.getIndexBlockByLastFarmingTx();
}

std::list<FarmingTransactionData> Blockchain::getFarmingTxs() const {
    return blockIndex.getAllLockedFarmingTransactions();
}

[[maybe_unused]] void Blockchain::process() {
    //
}

[[maybe_unused]] void Blockchain::updateBlockchain() {
    // TODONEW Messages::BlockCount request;
    // emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlockCount);
}

[[maybe_unused]] void Blockchain::checkBlockExistence(BlockVariant &block) {
    BlockVariant last = getLastBlock();

    /*
     * Blocks in blockchain are stored consistently, so if last block id
     * is greater than the coming block id - the last one is already in
     * blockchain. If ids are equals - trying to merge blocks.
     */
    if (last.getIndex() < block.getIndex() || last.isEmpty()) {
        addBlock(block);
        emit BlockIsMissing(block.getAny());
    } else if (last.getIndex() < block.getIndex()) {
        qDebug() << QString("Block [%1] already exists in local blockchain")
                        .arg(QString(block.getIndex().toByteArray()));
    } else if (last.getIndex() == block.getIndex()) {
        // blocks id's are equals -> merge blocks
        if (canMergeBlocks(last, block)) {
            Block merged = mergeBlocks(last.getAny(), block.getAny());
            if (merged.isEmpty())
                return;
            auto mergedVariant = BlockVariant(merged);
            addBlock(mergedVariant);
        }
    }
}

[[maybe_unused]] void Blockchain::blockCountResponse(const BigNumber &count) {
    if (blockIndex.getLastSavedId() < count
        || getBlock(SearchEnum::BlockParam::Id, count.toByteArray()).isEmpty()) {
        // TODONEW Messages::GetBlockMessage request;
        // request.param = SearchEnum::BlockParam::Id;
        // request.value = count.toByteArray();
        // emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
    }
}

BigNumber Blockchain::getBlockCount() {
    qDebug() << "BLOCKCHAIN: getBlockCount() count - " << this->blockIndex.getLastSavedId();

    return this->blockIndex.getLastSavedId();
}

void Blockchain::addBlockToBlockchain(BlockVariant &block) {
    addBlock(block);
    auto list = block.transactions();
    for (const auto &tmp : std::as_const(list)) {
        QList<ActorId> list;
        auto           accounts = node->accountController()->accounts();
        for (const auto &tmp : std::as_const(accounts))
            list.append(tmp->id());
        if (list.contains(tmp.getSender())) {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(),
                             Notification::NotifyType::TxToUser,
                             tmp.getReceiver().toByteArray() });
        } else if (list.contains(tmp.getReceiver())) {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(),
                             Notification::NotifyType::TxToMe,
                             tmp.getSender().toByteArray() });
        }
    }
}

void Blockchain::addGenBlockToBlockchain(GenesisBlock block) {
    auto blockVariant = BlockVariant(block);
    updateFirstId(blockVariant);
    blockIndex.addBlock(blockVariant);
    // emit sendMessage(block.serialize(), Messages::ChainMessage::GenesisBlockMessage);
}

// Actors //
std::shared_ptr<Actor<KeyPrivate>> Blockchain::getApprover() const {
    return node->accountController()->currentWallet();
}

[[maybe_unused]] void Blockchain::setApprover(const Actor<KeyPrivate> &value) {
    qFatal("Blockchain setApprover");
    // node->accountController()->currentWallet() = value;
}

[[maybe_unused]] void Blockchain::getTxFromBlockchain(
    const SearchEnum::TxParam &param,
    const QByteArray          &value,
    const std::string         &messageId,
    const QByteArray          &request) {
    Transaction transaction = getTransaction(param, value).first;
    if (!transaction.isEmpty()) {
        // TODONEW emit responseReady(transaction.serialize(), Messages::GeneralResponse::GetTxResponse,
        // request, receiver);
    } else {
        qDebug() << QString("The transaction with %1 parametr is not found").arg(SearchEnum::toString(param));
    }
}

void Blockchain::VerifyTx(Transaction &tx) {
    BlockVariant last         = getLastBlock();
    auto         lastBlockTxs = last.transactions();

    // check txs in the last block
    if (std::find(lastBlockTxs.begin(), lastBlockTxs.end(), tx) != lastBlockTxs.end()) {
        qDebug() << "New transaction can't be added: previous block contains it";
        return;
    }

    qDebug() << QString("New transaction [%1] is verified").arg(tx.toString());
    emit VerifiedTx(tx);
}

void Blockchain::proveTx(Transaction &tx) {
    qDebug() << "proveTx: started" << tx.getTypeTx();

    ActorId targetSender   = tx.getSender();
    ActorId targetReceiver = tx.getReceiver();
    // start reward check
    if (tx.isRewardTransaction() || tx.isFarmingTransaction()) {
        targetSender = tx.getApprover();
        // TODO: add extended check of validity
        auto res = this->blockIndex.getLastTxByData(tx.getData());
        if (res.second == "-1") {
            txManager->addProvedTransaction(tx);
            return;
        }
    }
    Actor<KeyPublic> senderActor;
    if (!targetSender.isEmpty())
        senderActor = node->actorIndex()->getActor(targetSender);
    Actor<KeyPublic> receiverActor;
    if (!targetReceiver.isEmpty())
        receiverActor = node->actorIndex()->getActor(targetReceiver);
    if (tx.getAmount() < 0) {
        qDebug() << "Transaction not approved: amount less 0";
        txManager->removeUnApprovedTransaction(tx);
        return;
    }
    if (targetSender == targetReceiver) {
        txManager->removeUnApprovedTransaction(tx);
        qDebug() << "Transaction not approved: sender == receiver";
        return;
    }
    if (tx.getAmount() == 0) {
        qDebug() << "Transaction not approved: amount == 0";
        // return;
    }

    // if receiver is not exist

    if ((receiverActor.empty() && !targetReceiver.isEmpty())
        || (senderActor.empty() && !targetSender.isEmpty())) {
        txManager->removeUnApprovedTransaction(tx);
        if (receiverActor.empty()) {
            txManager->addProvedTransaction(tx);
        }
        qDebug() << "Transaction not approved: receiver or sender is not exist";
        return;
    }

    // special conditions: receiver is null - coins burning
    if (targetSender.isEmpty()) {
        Actor<KeyPublic> producerActor;
        if (!tx.getProducer().isEmpty())
            producerActor = node->actorIndex()->getActor(tx.getProducer());
        else {
            qDebug() << QString("Tx %1 producer 0").arg(tx.getHash().c_str());
            txManager->removeUnApprovedTransaction(tx);
            return;
        }
        if (!producerActor.key().verify(tx.getHash(), tx.getSignature())) {
            qDebug() << QString("Tx %1 not approved: bad signature in fee tx").arg(tx.getHash().c_str());
            txManager->removeUnApprovedTransaction(tx);
            return;
        }
        if (tx.getAmount() <= 0) {
            qDebug() << QString("Tx %1 fee amount <= 0").arg(tx.getHash().c_str());
            txManager->removeUnApprovedTransaction(tx);
            return;
        }
        txManager->addProvedTransaction(tx);
        return;
    }

    //    // if !sig
    //    if (!senderActor.key().verify(tx->getDataForSignature().toStdString(),
    //    tx->getSignature().toStdString()))
    //    {
    //        qDebug() << "Tx" << tx->getHash() << "not approved: bad signature";
    //        txManager->removeUnApprovedTransaction(tx);
    //        return;
    //    }

    // special conditions: receiver is null - coins burning, contract creation
    if (targetReceiver.isEmpty()) {
        qDebug() << "target received is empty";
        tx.sign(node->accountController()->currentWallet());

        txManager->addProvedTransaction(tx);
        return;
    } else {
        if (tx.getData() == "InitContract") {
            return;
        }
        if (targetSender != node->actorIndex()->firstId()) {
            BigNumberFloat senderCurrentBalance = getUserBalance(targetSender, tx.getToken());
            senderCurrentBalance += txManager->checkPendingTxsList(targetSender);

            if (tx.getAmount() <= 0) {
                txManager->removeUnApprovedTransaction(tx);
                qDebug() << "Transaction not approved: amount <= 0";
                return;
            }

            auto    mainActorId = node->accountController()->mainActor()->id();
            ActorId firstId     = node->actorIndex()->firstId();
            if (senderCurrentBalance - tx.getAmount() - tx.getAmount() / 100 < 0 && mainActorId == firstId) {
                qDebug() << senderCurrentBalance << tx.getAmount();
                qDebug() << "Transaction "
                            "not approved: sender's or receiver's balance will be < 0";
                txManager->removeUnApprovedTransaction(tx);
                return;
            }

            txManager->addProvedTransaction(tx);
        } else {
            tx.sign(node->accountController()->currentWallet());
            txManager->addProvedTransaction(tx);
            return;
        }
        return;
    }
    qDebug() << "Undefine behaviour blockhain.cpp proveTx";
    txManager->removeUnApprovedTransaction(tx);
}

// Other //

BlockIndex &Blockchain::getBlockIndex() {
    return blockIndex;
}

void Blockchain::removeAll() {
    this->blockIndex.removeAll();
    QFile(DataStorage::TMP_GENESIS_BLOCK).remove();
}
