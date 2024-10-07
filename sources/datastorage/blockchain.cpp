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
// #include "datastorage/dfs/dfs_controller.h"
#include "datastorage/index/actorindex.h"
#include "managers/data_mining_manager.h"
#include "managers/transaction_manager.h"

#undef qCritical // temp
#define qCritical qDebug

Blockchain::Blockchain(ExtraChainNode &node) : node(node) {
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
        // node.network()->send_message(requestData, MessageType::BlockchainRequestBlock);
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

std::pair<Transaction, QByteArray> Blockchain::getTxByHash(const QByteArray &hash, const ActorId &token) {
    return blockIndex.getLastTxByHash(hash, token);
}

void Blockchain::sync() {
    auto lastBlock = getLastBlock();
    auto fromBlock = lastBlock.getIndex();
    if (fromBlock < 0)
        fromBlock = 0;
    qDebug() << "[Blockchain] Request sync from" << lastBlock.getIndex();
    node.network()->send_message(lastBlock.getIndex(), MessageType::BlockhainSync, MessageStatus::Request);
}

void Blockchain::syncResponse(const BigNumber fromBlock, const std::string &messageId) {
    BigNumber lastIndex = getLastBlock().getIndex();

    if (lastIndex < fromBlock) {
        this->sync();
        return;
    }

    BigNumber from = fromBlock;
    if (from < 0)
        from = 0;

    for(; from <= lastIndex; from++) {
        // qDebug() << "[Blockchain] Send sync" << from;
        BlockVariant block = blockIndex.getBlockById(from);

        if (block.isEmpty()) {
            continue;
        }

        if (block.isGenesisBlock()) {
            node.network()->send_message(block.getGenesisBlockConst(), MessageType::BlockchainGenesisBlock, MessageStatus::Response, messageId, Config::Net::TypeSend::Focused);
        } else {
            node.network()->send_message(block.getBlockConst(), MessageType::BlockchainNewBlock, MessageStatus::Response, messageId, Config::Net::TypeSend::Focused);
        }
    }

     qDebug() << "[Blockchain] Send sync from" << from << "to" << lastIndex;
}

std::pair<Transaction, QByteArray> Blockchain::getTxBySender(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxBySender(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByReceiver(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxByReceiver(id, token);
}

std::pair<Transaction, QByteArray>
Blockchain::getTxBySenderOrReceiver(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxBySenderOrReceiver(id, token);
}

std::pair<Transaction, QByteArray>
Blockchain::getTxBySenderOrReceiverAndToken(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxBySenderOrReceiverAndToken(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByApprover(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxByApprover(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByUser(const BigNumber &id, const ActorId &token) {
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
Blockchain::getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from, int count, ActorId token) {
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

    if (answerBlock.isEmpty()) {
        return;
    }

    if (answerBlock.getType() == BlockType::Genesis) {
        GenesisBlock genesisBlock = blockIndex.getGenesisBlockById(index);
        node.network()->send_message(genesisBlock, MessageType::BlockchainGenesisBlock);
    } else {
        auto dataBlock = answerBlock.getBlockConst();
        node.network()->send_message(dataBlock, MessageType::BlockchainNewBlock);
    }
}

void Blockchain::sendLastGenesisBlock() const {
    const GenesisBlock genesisBlock = blockIndex.getLastGenesisBlock();
    node.network()->send_message(genesisBlock, MessageType::BlockchainGenesisBlock);
}

// Genesis block //

bool Blockchain::shouldStartGenesisCreation() {
    auto lastBlock = getLastBlock();
    return lastBlock.getIndex() % Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS == 0;
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
            if (tx.getReceiver() == node.actorIndex()->firstId())
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
            nb.sign(node.accountController()->currentProfile().getActor(node.actorIndex()->firstId()));
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
        qDebug() << "[Blockchain] Blocks with id" << receivedBlockIndex << "can't be merged";
        return Errors::BLOCKS_CANT_MERGE;
    }

    qDebug() << "[Blockchain] Merging block" << receivedBlockIndex;
    if (received == existed && received.signatures() == existed.signatures()) {
        // qDebug() << "[Blockchain] Blocks" << receivedBlockIndex << "are equal";
        return Errors::BLOCKS_ARE_EQUAL;
    }

    // step 1 - create merged block
    if (received.isGenesisBlock() || existed.isGenesisBlock()) {
        qFatal("Please, check, error if try incorrect genesis merge");
        return Errors::BLOCKS_CANT_MERGE;
    }

    Block merged = mergeBlocks(received.getAny(), existed.getAny());

    if (merged.isEmpty())
        return Errors::BLOCKS_CANT_MERGE;

    // step - remove existed block (and all blocks after them)
    // and save updated blocks with new hash
    removeBlock(existed);
    auto mergedVariant = BlockVariant(merged);
    addBlock(mergedVariant);

    return 0;
}

int Blockchain::mergeGenesisBlockWithLocal(const GenesisBlock &received) {
    const auto   receivedIndex = received.getIndex();
    GenesisBlock existed       = blockIndex.getGenesisBlockById(receivedIndex);
    if (!existed.isEmpty()) {
        qCritical() << "Can't find genesis block with id" << receivedIndex << "locally";
        return Errors::NO_BLOCKS;
    }

    // saved block with the same id is genesis
    qDebug() << QString("Start merging genesis block [%1] with exising [%2]")
                    .arg(received.toString(), existed.toString());

    GenesisBlock merged = mergeGenesisBlocks(received, existed);

    // remove existed block (and all blocks after them)
    // and save updated blocks with new hash
    removeBlock(BlockVariant(existed));
    auto mergedVariant = BlockVariant(merged);
    addBlock(mergedVariant);

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
Blockchain::getTransaction(SearchEnum::TxParam type, const QByteArray &value, const ActorId &token) {
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
    return node.actorIndex()->validateBlock(block);
}

BlockVariant Blockchain::validateAndReturnBlock(const BlockVariant &block) const {
    // Get prev block hash and check if it exists in current one :)
    return block;
}

void Blockchain::updateFirstId(const BlockVariant &block) {
    if (!block.isGenesisBlock() || block.getIndex() != 0)
        return;

    if (block.dataService().size() > 1 || block.dataService().empty())
        qFatal("Incorrect first genesis");

    auto firstId = ActorId(*block.dataService().begin());
    if (!firstId.isZero())
        node.actorIndex()->setFirstId(firstId);
}

int Blockchain::addBlock(const BlockVariant &block) {
    if (block.isEmpty())
        return Errors::BLOCK_IS_NOT_VALID;

    if (block.getType() == BlockType::DataMerge || block.getType() == BlockType::GenesisMerge) {
        return Errors::BLOCK_IS_NOT_VALID;
    }

    const auto blockId = block.getIndex();
    if (block.isGenesisBlock() && blockId % Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS != 0) {
        qDebug() << "[Blockchain] Incorrect genesis";
        // qFatal("Incorrect genesis");
        return Errors::BLOCK_IS_NOT_VALID;
    }

    if (blockId != 0) {
        auto lastBlock = this->getBlockByIndex(blockId - 1);
        if (block.getPrevHash() != lastBlock.getHash()) {
            qDebug() << "[Blockchain] Can't chained";
            sync();
            return Errors::BLOCK_IS_NOT_VALID;
        }
    }

    if (block.getType() == BlockType::Genesis) {
        // qDebug() << "[Blockchain] Adding a genesis block" << block.getIndex() << "to storage";
    } else {
        // qDebug() << "[Blockchain] Adding a block" << block.getIndex() << "to storage" << block.getType();
    }

    if (block.getType() != BlockType::Genesis) {
        if (blockId != 0) {
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

    if (blockId < 0) {
        qFatal("Add block: index < 0");
        return Errors::BLOCK_IS_NOT_VALID;
    }

    auto newBlock(block);
    signBlock(newBlock);
    int        resultCode = blockIndex.addBlock(newBlock);
    const auto blockType  = newBlock.getType();

    switch (resultCode) {
    case 0: {
        if (blockType != BlockType::Dummy) {
            emit updateLastTransactionList();
        }
        qDebug() << "[Blockchain] Block" << blockId << "is successfully added to blockchain" << blockType;
        getSmContractMembers(newBlock);

        if (blockType == BlockType::Data) {
            saveTxInfoInEC(newBlock.transactions());
        }
        break;
    }
    case Errors::FILE_ALREADY_EXISTS: {
        // qDebug() << "[Blockchain] Block" << indexBlock << blockType << "is already in blockchain";
        if (blockType == BlockType::Data || blockType == BlockType::DataMerge
            || blockType == BlockType::Dummy) {
            resultCode = mergeBlockWithLocal(newBlock);
        } else if ((blockType == BlockType::Genesis) || (newBlock.getType() == BlockType::GenesisMerge)) {
            resultCode = mergeGenesisBlockWithLocal(*newBlock.getGenesisBlockConst());
        } else {
            qCritical() << "Unsupported block type in block: " << newBlock.getIndex();
        }
        break;
    }
    default:
        qCritical() << "[Blockchain] While adding a new block" << newBlock.toString();
    }

    if (blockId > 0 && blockId % DFS::Reward::coinProductionAlgorithmTick == 0) {
        node.dataMiningManager()->requestCoinReward();
    }

    // after adding genesis block we don't need to increment counter
    // if (!newBlock.isGenesisBlock() && resultCode == 0 /*&& newBlock.getType() != BlockType::Dummy*/) {
    //     if (shouldStartGenesisCreation()) {
    //         const auto actor = node.accountController()->mainActor();

    //         GenesisBlock gB = createGenesisBlock(actor);
    //         if (blockIndex.addBlock(BlockVariant(gB)) == 0) {
    //             qDebug() << "[Blockchain] Genesis block" << gB.getIndex()
    //                      << "is successfully added to blockchain" << gB.getType();
    //             // TODONEW emit sendMessage(gB.serialize(),
    //             // Messages::ChainMessage::GenesisBlockMessage);
    //         }
    //     }
    // }

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
    if (receivedBlock.getSignature().empty() || existedBlock.getSignature().empty()
        || receivedBlock.getType() != existedBlock.getType()
        || receivedBlock.getIndex() != existedBlock.getIndex()) {
        return false;
    }

    switch (receivedBlock.getType()) {
    case BlockType::Data:
    case BlockType::Genesis:
    case BlockType::Dummy:
        return true;
    case BlockType::GenesisMerge: {
        // 4) at least one common data row
        const auto &rowsA = receivedBlock.dataRows();
        const auto &rowsB = existedBlock.dataRows();

        for (const GenesisDataRow &rowA : rowsA) {
            if (rowsB.contains(rowA)) {
                return true;
            }
        }
        break;
    }
    case BlockType::DataMerge: {
        // 4) at least one common transaction
        const auto &transactionsA = receivedBlock.transactions();
        const auto &transactionsB = existedBlock.transactions();
        for (const Transaction &tr : transactionsA) {
            if (transactionsB.contains(tr)) {
                return true;
            }
        }
        break;
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

    BlockVariant prev = blockA.getIndex() == 0 ? BlockVariant(GenesisBlock()) : getBlockByIndex(blockA.getIndex() - 1);
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

        if (blockA.getIndex() != 0) {
            merged.setPrevGenHash(blockA.getPrevGenHash());
            merged.setPrev(prev);
        }

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
    block.sign(node.accountController()->currentWallet());
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

BigNumberFloat Blockchain::getUserBalance(ActorId userId, ActorId tokenId, TransactionType txType) const {
    BigNumberFloat balance;

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--) {
        BlockVariant currentBlock = blockIndex.getBlockById(i);

        if (currentBlock.getType() == BlockType::Dummy) {
            continue;
        }

        /*
        if (currentBlock.isGenesisBlock()) {
            GenesisBlock genesis = blockIndex.getGenesisBlockById(i);
            const auto   rows    = genesis.dataRows();

            for (const auto &row : rows) {
                if (userId == row.actorId)
                    balance += row.state;
            }

            return balance;
        }
        */

        if (currentBlock.isEmpty())
            break;

        auto txs = currentBlock.transactions();
        for (auto &tx : txs) {
            // if (tx.type() != txType)
            //     continue;

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
            node.actorIndex()->getActor(tx.getSender());
            node.actorIndex()->getActor(tx.getReceiver());
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

void Blockchain::setPossibleMining(const bool &value) {
    if (value != possibleMining) {
        emit possibleMiningChange(value);
    }
    possibleMining = value;
}

bool Blockchain::getPossibleMining() const {
    return possibleMining;
}

BigNumber Blockchain::getBlockCount() {
    qDebug() << "BLOCKCHAIN: getBlockCount() count - " << this->blockIndex.getLastSavedId();

    return this->blockIndex.getLastSavedId();
}

void Blockchain::addBlockFromNetwork(const BlockVariant &block) {
    BlockVariant lastBlock = this->getLastBlock();
    if (block.isEmpty()) {
        return;
    }
    // if (block.getType() == BlockType::DataMerge || block.getType() == BlockType::GenesisMerge)
    //     return;
    if (block.getType() != BlockType::Dummy && block.getType() != BlockType::Genesis) {
        this->removeAllDummyBlocks(lastBlock);
    }

    int resultCode = addBlock(block);

    if (resultCode == Errors::BLOCK_IS_NOT_VALID) {
        return;
    }

    if (resultCode != Errors::BLOCKS_ARE_EQUAL && resultCode != Errors::BLOCKS_CANT_MERGE && resultCode != Errors::NO_BLOCKS) {
        sendBlockByNumber(block.getIndex());
    }

    return; //
    auto list = block.transactions();
    for (const auto &tmp : std::as_const(list)) {
        QList<ActorId> list;
        auto           accounts = node.accountController()->accounts();
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

void Blockchain::addGenesisBlockFromNetwork(const GenesisBlock &block) {
    auto blockVariant = BlockVariant(block);
    updateFirstId(blockVariant);
    addBlock(blockVariant);
    // emit sendMessage(block.serialize(), Messages::ChainMessage::GenesisBlockMessage);
}

// Actors //
TransactionProveError Blockchain::proveTransaction(const Transaction &tx) {
    // qDebug() << "[Blockchain] Transaction prove started:" << tx;

    ActorId        targetSender   = tx.getSender();
    ActorId        targetReceiver = tx.getReceiver();
    const ActorId &mainActorId    = node.accountController()->mainActor()->id();
    const ActorId &firstId        = node.actorIndex()->firstId();

    const auto accounts = node.accountController()->accountsIds();
    for (const auto &accountId : accounts) {
        if (accountId == firstId)
            continue;
        if (targetSender == accountId || targetReceiver == accountId) {
            // reward check
            if (tx.isRewardTransaction()) {
                auto approverId = tx.getApprover();
                if (targetSender == ActorId() && !approverId.isZero()) {
                    auto approver = node.actorIndex()->getActor(approverId);
                    bool res = tx.verify(approver);
                    if (res) {
                        if (tx.getToken() != ActorId()) {
                            return TransactionProveError::RewardWrongToken;
                        }
                        if (tx.getAmount() == 0) {
                            return TransactionProveError::AmountZero;
                        }

                        // continue;
                        return TransactionProveError::NoError;
                    }
                }
            }

            return TransactionProveError::SelfPleasure;
        }
    }

    // reward check
    if (tx.isRewardTransaction()) {
        targetSender = tx.getApprover();
        // TODO: add extended check of validity
        auto res = this->blockIndex.getLastTxByData(tx.getData(), ActorId());

        if (tx.getToken() != ActorId()) {
            return TransactionProveError::RewardWrongToken;
        }

        if (res.second == "-1") {
            return TransactionProveError::NoError;
        }
    }
    Actor<KeyPublic> senderActor;
    if (!targetSender.isZero())
        senderActor = node.actorIndex()->getActor(targetSender);
    Actor<KeyPublic> receiverActor;
    if (!targetReceiver.isZero())
        receiverActor = node.actorIndex()->getActor(targetReceiver);
    if (tx.getAmount() < 0) {
        return TransactionProveError::AmountLessZero;
    }
    if (targetSender == targetReceiver) {
        return TransactionProveError::IdenticalSenderReceiver;
    }
    if (tx.getAmount() == 0) {
        return TransactionProveError::AmountZero;
    }

    auto block = getLastRealBlock();
    if (block.isEmpty()) {
        return TransactionProveError::EmptyBlockchain;
    }

    // if receiver is not exist
    if (senderActor.empty() && !targetSender.isZero()) {
        return TransactionProveError::SenderNotExists;
    }

    if (receiverActor.empty() && !targetReceiver.isZero()) {
        return TransactionProveError::ReceiverNotExists;
    }

    // special conditions: receiver is null - coins burning
    if (targetSender.isZero()) {
        Actor<KeyPublic> producerActor;
        if (!tx.getProducer().isZero())
            producerActor = node.actorIndex()->getActor(tx.getProducer());
        else {
            // return TransactionProveError::ZeroProducer;
        }

        if (!producerActor.key().verify(tx.getHash(), tx.getSignature())) {
            // return TransactionProveError::ProducerVerify;
        }

        return TransactionProveError::NoError;
    }

    //    // if !sig
    //    if (!senderActor.key().verify(tx->getDataForSignature().toStdString(),
    //    tx->getSignature().toStdString()))
    //    {
    //        qDebug() << "Tx" << tx->getHash() << "not approved: bad signature";
    //        node.transactionManager()->removeUnApprovedTransaction(tx);
    //        return;
    //    }

    // special conditions: receiver is null - coins burning, contract creation
    if (targetReceiver.isZero()) {
        qDebug() << "target received is empty";

        Transaction provedTx(tx);
        provedTx.sign(node.accountController()->currentWallet());
        return TransactionProveError::NoError;
    } else {
        if (tx.getData() == "InitContract") {
            return TransactionProveError::Unknown;
        }

        if (targetSender != firstId) {
            BigNumberFloat senderCurrentBalance = getUserBalance(targetSender, tx.getToken());
            senderCurrentBalance += node.transactionManager()->checkPendingTxsList(targetSender);

            BigNumberFloat transactionAmount = tx.getAmount();
            BigNumberFloat transactionFee    = 0; // transactionAmount / 100;
            BigNumberFloat senderNewBalance  = senderCurrentBalance - transactionAmount - transactionFee;

            if (senderNewBalance < 0 /* && mainActorId == firstId */) {
                return TransactionProveError::SenderBalanceBelowZero;
            }

            // sign?
            return TransactionProveError::NoError;
        } else {
            Transaction provedTx(tx);
            provedTx.sign(node.accountController()->currentWallet());
            return TransactionProveError::NoError;
        }

        return TransactionProveError::Unknown;
    }

    return TransactionProveError::Unknown;
}

// Other //

BlockIndex &Blockchain::getBlockIndex() {
    return blockIndex;
}

void Blockchain::removeAll() {
    this->blockIndex.removeAll();
    QFile(DataStorage::TMP_GENESIS_BLOCK).remove();
}
