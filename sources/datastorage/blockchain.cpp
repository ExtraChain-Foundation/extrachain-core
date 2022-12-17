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
#include "datastorage/index/actorindex.h"
#include "managers/data_mining_manager.h"
#include "managers/tx_manager.h"

#undef qCritical // temp
#define qCritical qDebug

Blockchain::Blockchain(ExtraChainNode *node, bool fileMode)
    : fileMode(fileMode) {
    this->node = node;
    genBlockData.clear();

    //    setCirculativeSupply(blockIndex.calculateCirculativeBalance());
    //    increaseCirculativeSupply(blockIndex.calculateCirculativeBalanceLastGenesisBlock());
}

Blockchain::~Blockchain() {
}

Block Blockchain::getBlockByIndex(const BigNumber &index) {
    Block block = fileMode ? blockIndex.getBlockById(index) : memIndex[index];
    //        Block block2 = validateAndReturnBlock(block);
    return block;
}

BigNumber Blockchain::checkIntegrity() {
    if (fileMode) {
        // check in MemIndex: start from second block
        for (int i = 1; i < memIndex.getRecords(); i++) {
            Block prev = memIndex.getByPosition(i - 1);
            Block cur = memIndex.getByPosition(i);
            if (cur.getPrevHash() != prev.getHash()) {
                return cur.getIndex();
            }
        }
    } else {
        // check in FileIndex: start from second block
        for (BigNumber i = 1; i < blockIndex.getRecords(); i++) {
            Block prev = blockIndex.getBlockByPosition(i - 1);
            Block cur = blockIndex.getBlockByPosition(i);
            if (cur.getPrevHash() != prev.getHash()) {
                return cur.getIndex();
            }
        }
    }
    return BigNumber();
}

void Blockchain::setTxManager(TransactionManager *value) {
    txManager = value;
}

// Blocks //

Block Blockchain::getLastBlock() const {
    Block block = fileMode ? blockIndex.getLastBlock() : memIndex.getLastBlock();
    return validateAndReturnBlock(block);
}

Block Blockchain::getLastRealBlock() const {
    Block block = fileMode ? blockIndex.getLastRealBlock() : memIndex.getLastBlock();
    return validateAndReturnBlock(block);
}

QByteArray Blockchain::getBlockDataByIndex(const BigNumber &index) {
    return blockIndex.getBlockDataById(index);
}

Block Blockchain::getBlockByApprover(const BigNumber &approver) {
    Block block = fileMode ? memIndex.getByApprover(approver) : blockIndex.getBlockByApprover(approver);
    return validateAndReturnBlock(block);
}

Block Blockchain::getBlockByData(const QByteArray &data) {
    Block block = fileMode ? blockIndex.getBlockByData(data) : memIndex.getByData(data);
    return validateAndReturnBlock(block);
}

Block Blockchain::getBlockByHash(const QByteArray &hash) {
    Block block = fileMode ? blockIndex.getBlockByHash(hash) : memIndex.getByHash(hash);
    return validateAndReturnBlock(block);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByHash(const QByteArray &hash, const QByteArray &token) {
    return fileMode ? blockIndex.getLastTxByHash(hash, token) : memIndex.getLastTxByHash(hash, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxBySender(const BigNumber &id, const QByteArray &token) {
    return fileMode ? blockIndex.getLastTxBySender(id, token) : memIndex.getLastTxBySender(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByReceiver(const BigNumber &id, const QByteArray &token) {
    return fileMode ? blockIndex.getLastTxByReceiver(id, token) : memIndex.getLastTxByReceiver(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxBySenderOrReceiver(const BigNumber &id,
                                                                       const QByteArray &token) {
    return fileMode ? blockIndex.getLastTxBySenderOrReceiver(id, token)
                    : memIndex.getLastTxBySenderOrReceiver(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxBySenderOrReceiverAndToken(const BigNumber &id,
                                                                               const QByteArray &token) {
    return fileMode ? blockIndex.getLastTxBySenderOrReceiverAndToken(id, token)
                    : memIndex.getLastTxBySenderOrReceiverAndToken(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByApprover(const BigNumber &id, const QByteArray &token) {
    return fileMode ? blockIndex.getLastTxByApprover(id, token) : memIndex.getLastTxByApprover(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByUser(const BigNumber &id, const QByteArray &token) {
    return fileMode ? blockIndex.getLastTxByApprover(id, token) : memIndex.getLastTxByApprover(id, token);
}

void Blockchain::saveTxInfoInEC(const std::string &data) const {
    std::vector<std::string> l = Serialization::deserialize(data);
    std::vector<DBRow> extractData;
    DBRow resultData;

    QString typeS = "0"; // sender type
    QString typeR = "0"; // receiver type

    DBConnector cacheDB("blockchain/cacheEC.db");
    cacheDB.open();
    cacheDB.createTable("CREATE TABLE IF NOT EXISTS cacheData"
                        " ("
                        "ActorId  TEXT   NOT NULL, "
                        "State     TEXT              NOT NULL, "
                        "Token     TEXT              NOT NULL, "
                        "Type   TEXT              NOT NULL );");

    for (const auto &i : l) {
        auto q = MessagePack::deserialize<Transaction>(i);

        // modify sender data in db
        extractData =
            cacheDB.select("SELECT State FROM cacheData WHERE ActorId ='" + q.getSender().toStdString()
                           + "' AND Token='" + q.getToken().toStdString() + "';");

        resultData["ActorId"] = q.getSender().toStdString();
        resultData["Token"] = q.getToken().toStdString();
        resultData["Type"] = typeS.toStdString();

        if (extractData.empty()) {
            resultData["State"] = '-' + q.getAmount().toStdString();
            cacheDB.insert("cacheData", resultData);
        }

        else {
            resultData["State"] = (BigNumberFloat(extractData[0]["State"]) - q.getAmount()).toStdString();
            cacheDB.update("UPDATE cacheData "
                           "SET State ='"
                           + resultData["State"] + "' WHERE ActorId ='" + resultData["ActorId"]
                           + "' AND Token='" + resultData["Token"] + "';");
        }

        extractData.clear();
        resultData.clear();

        // modify receiver data in db
        extractData =
            cacheDB.select("SELECT State FROM cacheData WHERE ActorId ='" + q.getReceiver().toStdString()
                           + "' AND Token='" + q.getToken().toStdString() + "';");

        resultData["ActorId"] = q.getReceiver().toStdString();
        resultData["Token"] = q.getToken().toStdString();
        resultData["Type"] = typeR.toStdString();
        if (extractData.empty()) {
            resultData["State"] = q.getAmount().toStdString();
            cacheDB.insert("cacheData", resultData);
        }

        else {
            resultData["State"] = (BigNumberFloat(extractData[0]["State"]) + q.getAmount()).toStdString();
            cacheDB.update("UPDATE cacheData "
                           "SET State ='"
                           + resultData["State"] + "' WHERE ActorId='" + resultData["ActorId"]
                           + "' AND Token='" + resultData["Token"] + "';");
        }
    }
}

QList<Transaction> Blockchain::getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from, int count,
                                                             BigNumber token) {
    return /*fileMode ?*/ blockIndex.getTxsBySenderOrReceiverInRow(id, from, count, token);
    // : memIndex.getLastTxBySenderOrReceiver(id);
}

void Blockchain::getBlockZero() {
    Block zero = getBlockByIndex(0);
    if (zero.isEmpty()) {
        // TODONEW
        // Messages::GetBlockMessage request;
        // request.param = SearchEnum::BlockParam::Id;
        // request.value = QByteArray::number(0);
        // emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
    } else
        node->actorIndex()->setFirstId(zero.getApprover());
}

BigNumber Blockchain::getSupply(const QByteArray &idToken) {
    GenesisBlock gen = blockIndex.getLastGenesisBlock();
    BigNumber id = gen.getIndex();
    std::string path = blockIndex.buildFilePath(id).toStdString();
    DBConnector cacheDB(path);
    cacheDB.open();
    std::vector<DBRow> extractData =
        cacheDB.select("SELECT * FROM GenesisDataRow WHERE token = '" + idToken.toStdString() + "';");
    BigNumber res = 0;
    for (const auto &tmp : extractData) {
        res += BigNumber(tmp.at("state")).abs();
    }
    return res;
}

BigNumber Blockchain::getFullSupply(const QByteArray &idToken) {
    BigNumber id = blockIndex.getLastGenesisBlock().getIndex();
    std::string path = blockIndex.buildFilePath(id).toStdString();
    DBConnector cacheDB(path);
    cacheDB.open();
    std::vector<DBRow> extractData =
        cacheDB.select("SELECT * FROM GenesisDataRow WHERE token = '" + idToken.toStdString() + "' ;");
    BigNumber res = 0;
    for (const auto &tmp : extractData) {
        res += BigNumber(tmp.at("state")).abs();
    }
    DBConnector cacheDB2("blockchain/cacheEC.db");
    cacheDB2.open();
    std::vector<DBRow> extractData2 = cacheDB2.select(
        "SELECT * FROM cacheData WHERE Token = '"
        + idToken.toStdString()
        /* + "' AND ActorId != '" + actorIndex->m_firstId->toStdString() + "' AND ActorId != '"
         + BigNumber(0).toActorId().toStdString()*/
        + "';");
    for (const auto &tmp : extractData2) {
        std::string sum = tmp.at("State");
        if (sum[0] == '-')
            continue;
        res += BigNumber(sum).abs();
    }
    return res;
}

Block Blockchain::checkBlock(const Block &block) {
    if (GenesisBlock::isGenesisBlock(block.serialize()))
        return GenesisBlock(block.serialize());
    else
        return Block(block);
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
                genBlockData.append(row1);
            }
            if (!b2) {
                genBlockData.append(row2);
            }
            return;
        }
    }
}

QByteArray Blockchain::findRecordsInBlock(const Block &block) {
    if (block.getType() == Config::GENESIS_BLOCK_TYPE) {
        return QByteArray::fromStdString(block.getHash());
    } else if (!block.isEmpty()) {
        const auto transactions = block.extractTransactions();
        for (const Transaction &tx : qAsConst(transactions)) {
            if (tx.getReceiver() == node->actorIndex()->firstId())
                break;
            GenesisDataRow recSender =
                GenesisDataRow(tx.getSender(), getUserBalance(tx.getSender(), tx.getToken()), tx.getToken(),
                               DataStorage::typeDataRow::UNIVERSAL);
            GenesisDataRow recReceiver =
                GenesisDataRow(tx.getReceiver(), getUserBalance(tx.getReceiver(), tx.getToken()),
                               tx.getToken(), DataStorage::typeDataRow::UNIVERSAL);
            addRecordsIfNew(recReceiver, recSender);
        }
    }
    return QByteArray();
}

bool Blockchain::signCheckAdd(Block &block) {
    if (block.getIndex() == 0)
        return false;
    QByteArrayList list = block.getListSignatures();

    int count = 0;
    if (block.getType() == "genesis") {
        GenesisBlock saved(getBlockData(SearchEnum::BlockParam::Id, block.getIndex().toByteArray()));

        QByteArrayList savedList = saved.getListSignatures();
        if (!savedList.isEmpty()) {
            if (list != savedList) {
                for (int i = 0; i < list.size(); i += 3) {
                    if (!savedList.contains(list[i])) {
                        QString path = blockIndex.buildFilePath(block.getIndex());
                        DBConnector DB(path.toStdString());
                        DB.open();
                        DBRow rowRow;
                        rowRow.insert({ "actorId", list[i].toStdString() });
                        rowRow.insert({ "digSig", list[i + 1].toStdString() });
                        rowRow.insert({ "type", list[i + 2].toStdString() });
                        DB.insert(Config::DataStorage::SignTable, rowRow);
                        count++;
                    }
                }
                if (count != 0)
                    return true;
            }
        }

        if ((list.size() / 3) >= COUNT_APPROVER_BLOCK) {
            if ((list.size() / 3) >= COUNT_CHECKER_BLOCK)
                return false;
            QByteArray id = node->accountController()->mainActor().id().toByteArray();
            if (!list.contains(id)) {
                QByteArray sign = QByteArray::fromStdString(
                    node->accountController()->mainActor().key().sign(block.getHash()));
                block.addSignature(id, sign, false);
                return true;
            }
        }
        //        else
        //        {
        //            block.sign(*accountController->getMainActor());
        //        }
    } else {
        const auto blockData = getBlockData(SearchEnum::BlockParam::Id, block.getIndex().toByteArray());
        if (blockData.isEmpty())
            return false;
        Block saved(blockData);
        QByteArrayList savedList = saved.getListSignatures();
        if (!savedList.isEmpty()) {
            if (list != savedList) {
                for (int i = 0; i < list.size(); i += 3) {
                    if (!savedList.contains(list[i])) {
                        QString path = blockIndex.buildFilePath(block.getIndex());
                        DBConnector DB(path.toStdString());
                        DB.open();
                        DBRow rowRow;
                        rowRow.insert({ "actorId", list[i].toStdString() });
                        rowRow.insert({ "digSig", list[i + 1].toStdString() });
                        rowRow.insert({ "type", list[i + 2].toStdString() });
                        DB.insert(Config::DataStorage::SignTable, rowRow);
                        count++;
                    }
                }
            }
        }
        if ((list.size() / 3) >= COUNT_APPROVER_BLOCK) {
            if ((list.size() / 3) > COUNT_CHECKER_BLOCK + COUNT_APPROVER_BLOCK)
                return false;
            QByteArray id = node->accountController()->mainActor().id().toByteArray();
            if (!list.contains(id)) {
                QByteArray sign = QByteArray::fromStdString(
                    node->accountController()->mainActor().key().sign(block.getHash()));
                block.addSignature(id, sign, false);
                return true;
            }
        }
    }
    return false;
}

GenesisBlock Blockchain::createGenesisBlock(const Actor<KeyPrivate> actor, QMap<ActorId, BigNumber> states) {
    qDebug() << "Creating genesis block";
    genBlockData.clear();
    // QByteArray previousGenHash;
    GenesisBlock nb("", Block(), "");
    if (fileMode) {
        if (blockIndex.getLastSavedId().isEmpty()) {
            qCritical() << "Can't create genesis block, there no last saved id";
            return nb;
        }
        if (blockIndex.getRecords() == 0) {
            if (blockIndex.getFirstSavedId() == 0 && blockIndex.getLastSavedId() == 0) {
                for (auto i = states.begin(); i != states.end(); i++) {
                    genBlockData.append(
                        GenesisDataRow(i.key(), i.value(), ActorId(), DataStorage::typeDataRow::UNIVERSAL));
                }

                // nb.setApprover(BigNumber(*(actorIndex->m_firstId)));
                nb.sign(node->accountController()->currentProfile().getActor(node->actorIndex()->firstId()));
            } else
                qCritical() << "Can't create genesis block, there no blocks in blockIndex";
            return nb;
        } else {
            Block b;
            BigNumber i = blockIndex.getLastSavedId();
            nb = GenesisBlock("", blockIndex.getBlockById(blockIndex.getLastSavedId()), "");
            while ((blockIndex.getBlockById(i).getType() != Config::GENESIS_BLOCK_TYPE)
                   && (i >= blockIndex.getFirstSavedId())) {
                b = blockIndex.getBlockById(i);
                findRecordsInBlock(b);
                i--;
            }
            DBConnector cacheDB("blockchain/cacheEC.db");
            cacheDB.open();
            std::vector<DBRow> extractData = cacheDB.select("SELECT * FROM cacheData;");
            for (auto i : extractData)
                nb.addRow(
                    GenesisDataRow(i["ActorId"], BigNumber(i["State"]), i["Token"],
                                   DataStorage::typeDataRow(QByteArray::fromStdString(i["Type"]).toInt())));
            cacheDB.query("DELETE FROM cacheData");
            cacheDB.query("VACUUM");
            nb.setPrevGenHash(blockIndex.getBlockById(i).getHash());
        }
        qDebug() << "Genesis block created";
        genBlockData.clear();
        nb.sign(actor);
        return nb;
    } else
        return GenesisBlock();
}

// Merging //

int Blockchain::mergeBlockWithLocal(Block &received) {
    const auto receivedBlockIndex = received.getIndex();
    Block existed = getBlockByIndex(receivedBlockIndex);
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
    Block merged = mergeBlocks(received, existed);

    if (merged.isEmpty())
        return Errors::BLOCKS_CANT_MERGE;

    // step 2 - collect all blocks from old to latest
    QList<Block> tmpBlocks; // from existed to last block;

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
    for (Block &b : tmpBlocks) {
        if (b.getPrevHash() == oldHash) {
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
    for (Block &b : tmpBlocks) {
        addBlock(b);
    }
    //  emit SendMergedBlock(existed, received, merged);
    return 0;
}

int Blockchain::mergeGenesisBlockWithLocal(const GenesisBlock &received) {
    const auto receivedIndex = received.getIndex();
    GenesisBlock existed = blockIndex.getGenesisBlockById(receivedIndex);
    if (!existed.isEmpty()) {
        // saved block with the same id is genesis
        qDebug() << QString("Start merging genesis block [%1] with exising [%2]")
                        .arg(received.toString(), existed.toString());

        // step 1
        GenesisBlock merged = mergeGenesisBlocks(received, existed);

        // step 2 - collect all blocks from old to latest
        QList<Block> tmpBlocks; // from existed to last block;

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
        for (Block &b : tmpBlocks) {
            if (b.getPrevHash() == oldHash) {
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
        for (Block &b : tmpBlocks) {
            addBlock(b);
        }
    } else {
        qCritical() << "Can't find genesis block with id=" << receivedIndex << "locally";
        return Errors::NO_BLOCKS;
    }
    return 0;
}

Block Blockchain::getBlock(SearchEnum::BlockParam type, const QByteArray &value) {
    Block res;
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
    case SearchEnum::BlockParam::Approver:
        res = getBlockByApprover(BigNumber(value.toStdString()));
        break;
    default:
        res = Block();
        break;
    }
    return res;
}

QByteArray Blockchain::getBlockData(SearchEnum::BlockParam type, const QByteArray &value) {
    QByteArray res = "";
    switch (type) {
    case SearchEnum::BlockParam::Id:
        res = getBlockDataByIndex(BigNumber(value.toStdString()));
        break;
    default:
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

bool Blockchain::validateBlock(const Block &block) {
    return node->actorIndex()->validateBlock(block);
}

Block Blockchain::validateAndReturnBlock(const Block &block) const {
    // Get prev block hash and check if it exists in current one :)
    return block;
}

int Blockchain::addBlock(Block &block, bool isGenesis) {
    if (isGenesis) {
        qDebug() << "Adding a GENESIS block" << block.getIndex() << "to storage";
    } else {
        qDebug() << "Adding a block" << block.getIndex() << "to storage";
    }

    const auto indexBlock = block.getIndex();
    if (!GenesisBlock::isGenesisBlock(block.serialize())) {
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
    if (indexBlock == 0) {
        node->actorIndex()->setFirstId(block.getApprover());
    }
    if (indexBlock < 0)
        return Errors::BLOCK_IS_NOT_VALID;

    bool check = signCheckAdd(block);
    if (check) {
        // TODONEW emit sendMessage(block.serialize(), Messages::ChainMessage::BlockMessage);
    }
    int resultCode = fileMode ? blockIndex.addBlock(block) : memIndex.addBlock(block);
    const auto blockType = block.getType();

    switch (resultCode) {
    case 0: {
        emit updateLastTransactionList(); // TODO: ?
        qDebug() << "Block" << indexBlock << "is successfully added to blockchain";
        getSmContractMembers(block);

        // TODONEW emit sendMessage(block.serialize(), Messages::ChainMessage::BlockMessage);
        if (blockType == Config::DATA_BLOCK_TYPE) {
            saveTxInfoInEC(block.getData());
        }
        qDebug() << (blockType == Config::DATA_BLOCK_TYPE) << blockType.c_str();
        node->dataMiningManager()->coinRewardRequest(indexBlock);

        break;
    }
    case Errors::FILE_ALREADY_EXISTS: {
        qDebug() << "Block" << indexBlock << blockType.c_str() << "is already in blockchain";
        if (blockType == Config::DATA_BLOCK_TYPE || blockType == Config::MERGE_BLOCK
            || blockType == Config::DUMMY_BLOCK_TYPE) {
            resultCode = mergeBlockWithLocal(block);
        } else if ((blockType == Config::GENESIS_BLOCK_TYPE)
                   || (block.getType() == Config::GENESIS_BLOCK_MERGE)) {
            resultCode = mergeGenesisBlockWithLocal(dynamic_cast<const GenesisBlock &>(block));
        } else {
            qCritical() << "Unsupported block type in block: " << block.getIndex();
        }
        break;
    }
    default:
        qCritical() << "While adding a new block" << block.toString();
    }

    // after adding genesis block we don't need to increment counter
    if (!isGenesis && resultCode == 0) {
        blocksFromLastGenesis++;
        if (shouldStartGenesisCreation()) {
            GenesisBlock gB = createGenesisBlock(node->accountController()->mainActor());
            if (blockIndex.addBlock(gB) == 0) {
                qDebug() << "Block" << gB.getIndex() << QByteArray::fromStdString(gB.getType())
                         << "is successfully added to blockchain";
                // TODONEW emit sendMessage(gB.serialize(),
                // Messages::ChainMessage::GenesisBlockMessage);
                blocksFromLastGenesis = 0;
            }
        }
    }

    return resultCode;
}

int Blockchain::removeBlock(const Block &block) {
    return fileMode ? blockIndex.removeById(block.getIndex()) : memIndex.removeById(block.getIndex());
}

void Blockchain::removeAllDummyBlocks(const Block &block) {
    blockIndex.removeDummyBlocks(block.getIndex());
}

bool Blockchain::canMergeBlocks(const Block &receivedBlock, const Block &existedBlock) {
    // 1) Blocks are approved
    // 2) Blocks has one type
    // 3) Blocks ids are identical
    if (!receivedBlock.getDigSig().empty() && !existedBlock.getDigSig().empty()
        && receivedBlock.getType() == existedBlock.getType()
        && receivedBlock.getIndex() == existedBlock.getIndex()) {
        if ((receivedBlock.getType() == Config::DATA_BLOCK_TYPE)
            || (receivedBlock.getType() == Config::GENESIS_BLOCK_TYPE)
            || (receivedBlock.getType() == Config::DUMMY_BLOCK_TYPE))
            return true;
        else if (receivedBlock.getType() == Config::GENESIS_BLOCK_MERGE) {
            // 4) at least one common data row
            QList<GenesisDataRow> rowsA = dynamic_cast<const GenesisBlock &>(receivedBlock).extractDataRows();
            QList<GenesisDataRow> rowsB = dynamic_cast<const GenesisBlock &>(existedBlock).extractDataRows();
            for (const GenesisDataRow &g : rowsA) {
                if (rowsB.contains(g)) {
                    return true;
                }
            }
        } else if (receivedBlock.getType() == Config::MERGE_BLOCK) {
            // 4) at least one common transaction
            auto transactionsA = receivedBlock.extractTransactions();
            auto transactionsB = existedBlock.extractTransactions();
            for (const Transaction &tr : transactionsA) {
                if (std::find(transactionsB.begin(), transactionsB.end(), tr) != transactionsB.end()) {
                    return true;
                }
            }
        }
    }
    return false;
}

Block Blockchain::mergeBlocks(const Block &blockA, const Block &blockB) {
    qDebug() << "Attempting to merge block:" << blockA.serialize() << "and block:" << blockB.serialize();

    if (blockA.getIndex() == BigNumber(0))
        return Block();
    Block prev = getBlockByIndex(blockA.getIndex() - 1);
    if (prev.isEmpty()) {
        qWarning() << "Can't merge block" << blockA.toString() << "with" << blockB.toString()
                   << " - there no prev block";
        return Block();
    }

    const std::string dataA = blockA.getData();
    const std::string dataB = blockB.getData();

    // Case 1 - equal payload
    if (dataA == dataB) {
        Block merged(QByteArray::fromStdString(dataA), prev);
        signBlock(merged);
        return merged;
    } else // Case 2 - different payload
    {
        auto transactionsA = blockA.extractTransactions();
        auto transactionsB = blockB.extractTransactions();
        // ListContainer<Transaction> txs;
        auto resultList = transactionsA;

        for (const Transaction &tx : qAsConst(transactionsA)) {
            if (std::find(transactionsB.begin(), transactionsB.end(), tx) == transactionsB.end())
                resultList.push_back(tx);
        }
        std::vector<std::string> list;
        for (const Transaction &tx : resultList)
            list.push_back(tx.serialize());
        std::string dataBlock = Serialization::serialize(list);
        Block mergedBlock(dataBlock, prev);
        signBlock(mergedBlock);
        return mergedBlock;
    }
}

GenesisBlock Blockchain::mergeGenesisBlocks(const GenesisBlock &blockA, const GenesisBlock &blockB) {
    qDebug() << "Attempting to merge genesis block:" << blockA.serialize()
             << "and block:" << blockB.serialize();

    Block prev = getBlockByIndex(blockA.getIndex() - 1);

    if (prev.isEmpty()) {
        qWarning() << "Can't merge genesis block" << blockA.toString() << "with" << blockB.toString()
                   << " - there no prev block";
        return GenesisBlock();
    }

    QByteArray dataA = QByteArray::fromStdString(blockA.getData());
    const QByteArray dataB = QByteArray::fromStdString(blockB.getData());

    // Case 1 - equal payload
    if (dataA == dataB) {
        GenesisBlock merged(dataA, prev, QByteArray::fromStdString(blockA.getPrevGenHash()));
        signBlock(merged);
        return merged;
    } else // Case 2 - different payload
    {
        // todo: make utils::combine(list, list) function;
        QList<GenesisDataRow> genDataRowsA = blockA.extractDataRows();
        QList<GenesisDataRow> genDataRowsB = blockB.extractDataRows();
        QList<GenesisDataRow> resultList = genDataRowsA;
        int count = 0;
        for (const GenesisDataRow &r : qAsConst(genDataRowsA)) {
            if (!genDataRowsB.contains(r)) {
                resultList.append(r);
            } else
                count++;
        }
        std::vector<std::string> list;
        if (count < Config::NECESSARY_SAME_TX)
            return GenesisBlock();
        for (const GenesisDataRow &gn : resultList)
            list.push_back(gn.serialize());
        std::string genData = Serialization::serialize(list);
        GenesisBlock mergedBlock(QByteArray::fromStdString(genData), prev,
                                 QByteArray::fromStdString(blockA.getPrevGenHash()));
        signBlock(mergedBlock);
        return mergedBlock;
    }
}

void Blockchain::signBlock(Block &block) const {
    block.sign(node->accountController()->currentWallet());
}

BigNumber Blockchain::getBlockChainLength() const {
    return fileMode ? blockIndex.getRecords() : memIndex.getRecords();
}

QString Blockchain::getLastBlockData() const {
    return fileMode ? QString::fromStdString(blockIndex.getLastBlock().getData())
                    : QString::fromStdString(memIndex.getLastBlock().getData());
}

BigNumber Blockchain::getRecords() const {
    return fileMode ? blockIndex.getRecords() : memIndex.getRecords();
}

BigNumberFloat Blockchain::getUserBalance(ActorId userId, ActorId tokenId) const {
    BigNumberFloat balance;

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--) {
        Block currentBlock = blockIndex.getBlockById(i);

        if (GenesisBlock::isGenesisBlock(currentBlock.serialize())) {
            GenesisBlock genesis = blockIndex.getGenesisBlockById(i);
            const auto rows = genesis.extractDataRows();

            for (const auto &row : rows) {
                if (userId == row.actorId)
                    return balance + row.state;
            }

            return balance;
        }

        if (currentBlock.isEmpty())
            break;

        auto txs = currentBlock.extractTransactions();

        for (auto &tx : txs) {
            if (tx.getReceiver() == userId && tx.getToken() == tokenId) {
                balance += tx.getAmount();
            }
        }
    }

    return balance;
}

void Blockchain::showBlockchain() const {
    qDebug() << "BLOCKCHAIN: showBlockchain()";
    int i = 0;
    Block currentBlock = blockIndex.getBlockById(i);
    do {
        i++;
        currentBlock = blockIndex.getBlockById(i);
    } while (!currentBlock.isEmpty());
    GenesisBlock genBlock = blockIndex.getLastGenesisBlock();
    qDebug() << "Genesis block: ";

    auto extra = genBlock.extractDataRows();
    for (const auto &dataGen : qAsConst(extra)) {
        qDebug() << &dataGen;
    }
}

bool Blockchain::isSmContractTx(const Block &block) const {
    if (block.getData() == "InitContract")
        return true;
    return false;
}

void Blockchain::getSmContractMembers(const Block &block) const {
    if (!isSmContractTx(block))
        return;
    auto txList = block.extractTransactions();
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

void Blockchain::sendCoinReward(const ActorId &receiver, const int &amount, const std::string &messageId) {
    auto mainActor = node->accountController()->mainActor();
    if (mainActor.id() == node->actorIndex()->firstId()) {
        Transaction tx;
        tx.setSender(mainActor.id());
        tx.setReceiver(receiver);
        tx.setAmount(amount);
        node->network()->send_message(tx, MessageType::BlockchainTransaction);
    } else {
        //        DFSR::CoinReward coinReward = DFSR::CoinReward { .Actor = receiver.toStdString(), .Coin =
        //        amount }; node->network()->send_message(coinReward, MessageType::BlockchainCoinReward,
        //        MessageStatus::Response,
        //                                      messageId, Config::Net::TypeSend::All);
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

void Blockchain::process() {
    //
}

void Blockchain::updateBlockchain() {
    // TODONEW Messages::BlockCount request;
    // emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlockCount);
}

void Blockchain::checkBlockExistence(Block &block) {
    Block last = getLastBlock();

    /*
     * Blocks in blockchain are stored consistently, so if last block id
     * is greater than the coming block id - the last one is already in
     * blockchain. If ids are equals - trying to merge blocks.
     */
    if (last.getIndex() < block.getIndex() || last.isEmpty()) {
        addBlock(block);
        emit BlockIsMissing(block);
    } else if (last.getIndex() < block.getIndex()) {
        qDebug() << QString("Block [%1] already exists in local blockchain")
                        .arg(QString(block.getIndex().toByteArray()));
    } else if (last.getIndex() == block.getIndex()) {
        // blocks id's are equals -> merge blocks
        if (canMergeBlocks(last, block)) {
            Block merged = mergeBlocks(last, block);
            if (merged.isEmpty())
                return;
            addBlock(merged);
            //      emit SendMergedBlock(last, block, merged);
        }
    }
}

void Blockchain::blockCountResponse(const BigNumber &count) {
    if (blockIndex.getLastSavedId() < count
        || getBlock(SearchEnum::BlockParam::Id, count.toByteArray()).isEmpty()) {
        // TODONEW Messages::GetBlockMessage request;
        // request.param = SearchEnum::BlockParam::Id;
        // request.value = count.toByteArray();
        // emit sendMessage(request.serialize(), Messages::GeneralRequest::GetBlock);
    }
}

void Blockchain::getBlockFromBlockchain(const SearchEnum::BlockParam &param, const QByteArray &value,
                                        const QByteArray &requestHash, const std::string &messageId) {
    QByteArray srBlock = getBlockData(param, value);
    if (srBlock.isEmpty())
        return;
    // TODONEW emit responseReady(srBlock, Messages::GeneralResponse::GetBlockResponse, requestHash,
    // receiver);
}

void Blockchain::getBlockCount(const QByteArray &requestHash, const std::string &messageId) {
    qDebug() << "BLOCKCHAIN: getBlockCount() count - " << this->blockIndex.getLastSavedId();

    //
    QByteArray lastSavedId = this->blockIndex.getLastSavedId().toByteArray();
    BigNumber res = this->blockIndex.getRecords();

    QJsonObject obj;
    obj["lastId"] = QString(lastSavedId);
    obj["count"] = QString(res.toByteArray());
    // QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    // TODONEW emit responseReady(this->blockIndex.getLastSavedId().toByteArray(),
    // Messages::GeneralResponse::GetBlockCountResponse, requestHash, receiver);
}

void Blockchain::addBlockToBlockchain(Block &block) {
    addBlock(block);
    auto list = block.extractTransactions();
    for (const auto &tmp : qAsConst(list)) {
        QList<ActorId> list;
        auto accounts = node->accountController()->accounts();
        for (const auto &tmp : qAsConst(accounts))
            list.append(tmp.id());
        //        std::string data = tmp.getData();
        if (list.contains(tmp.getSender())) {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(), Notification::NotifyType::TxToUser,
                             tmp.getReceiver().toByteArray() });
        } else if (list.contains(tmp.getReceiver())) {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(), Notification::NotifyType::TxToMe,
                             tmp.getSender().toByteArray() });
        }
    }
}

void Blockchain::addGenBlockToBlockchain(GenesisBlock block) {
    QMutex mutex;
    if (block.getIndex() == 0) {
        mutex.lock();
        node->actorIndex()->setFirstId(block.getApprover());
        mutex.unlock();
    }
    if (blockIndex.addBlock(block) == 0 || signCheckAdd(block)) {
        // TODONEW emit sendMessage(block.serialize(), Messages::ChainMessage::GenesisBlockMessage);
    }
}

// Actors //
Actor<KeyPrivate> Blockchain::getApprover() const {
    return node->accountController()->currentWallet();
}

void Blockchain::setApprover(const Actor<KeyPrivate> &value) {
    qFatal("Blockchain setApprover");
    // node->accountController()->currentWallet() = value;
}

void Blockchain::getTxFromBlockchain(const SearchEnum::TxParam &param, const QByteArray &value,
                                     const std::string &messageId, const QByteArray &request) {
    Transaction transaction = getTransaction(param, value).first;
    if (!transaction.isEmpty()) {
        // TODONEW emit responseReady(transaction.serialize(), Messages::GeneralResponse::GetTxResponse,
        // request, receiver);
    } else {
        qDebug() << "The transaction with" << SearchEnum::toString(param) << "parametr is not found";
    }
}

void Blockchain::VerifyTx(Transaction &tx) {
    Block last = getLastBlock();
    auto lastBlockTxs = last.extractTransactions();

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

    ActorId targetSender = tx.getSender();
    ActorId targetReceiver = tx.getReceiver();
    // start reward check
    if (tx.isRewardTransaction()) {
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

    // if receiver is not exist

    if ((receiverActor.empty() && !targetReceiver.isEmpty())
        || (senderActor.empty() && !targetSender.isEmpty())) {
        txManager->removeUnApprovedTransaction(tx);
        qDebug() << "Transaction not approved: receiver or sender is not exist";
        return;
    }

    // special conditions: receiver is null - coins burning
    if (targetSender.isEmpty()) {
        Actor<KeyPublic> producerActor;
        if (!tx.getProducer().isEmpty())
            producerActor = node->actorIndex()->getActor(tx.getProducer());
        else {
            qDebug() << "Tx" << tx.getHash().c_str() << "producer 0";
            txManager->removeUnApprovedTransaction(tx);
            return;
        }
        if (!producerActor.key().verify(tx.getDataForDigSig(), tx.getDigSig())) {
            qDebug() << "Tx" << tx.getHash().c_str() << "not approved: bad signature in fee tx";
            txManager->removeUnApprovedTransaction(tx);
            return;
        }
        if (tx.getAmount() <= 0) {
            qDebug() << "Tx" << tx.getHash().c_str() << "fee amount <= 0";
            txManager->removeUnApprovedTransaction(tx);
            return;
        }
        txManager->addProvedTransaction(tx);
        return;
    }

    //    // if !sig
    //    if (!senderActor.key().verify(tx->getDataForDigSig().toStdString(), tx->getDigSig().toStdString()))
    //    {
    //        qDebug() << "Tx" << tx->getHash() << "not approved: bad signature";
    //        txManager->removeUnApprovedTransaction(tx);
    //        return;
    //    }

    // special conditions: receiver is null - coins burning, contract creation
    if (targetReceiver.isEmpty()) {
        //
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

            auto mainActorId = node->accountController()->mainActor().id();
            ActorId firstId = node->actorIndex()->firstId();
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

void Blockchain::setMode(bool fileMode) {
    this->fileMode = fileMode;
}

MemIndex &Blockchain::getMemIndex() {
    return memIndex;
}

BlockIndex &Blockchain::getBlockIndex() {
    return blockIndex;
}

void Blockchain::removeAll() {
    // node->actorIndex()->removeAll();
    this->memIndex.removeAll();
    this->blockIndex.removeAll();
    QFile(DataStorage::TMP_GENESIS_BLOCK).remove();
}
