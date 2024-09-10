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

#include "datastorage/index/blockindex.h"
#include <QDir>
#include <QFileInfoList>

BlockIndex::BlockIndex() {
    this->folderName = DataStorage::BLOCK_INDEX_FOLDER_NAME;
    this->sectionSize = Config::DataStorage::SECTION_SIZE;
    firstSavedId = loadFirstId();
    lastSavedId = loadLastId();
    QDir dir(DataStorage::BLOCKCHAIN_INDEX + '/' + folderName);
    QFileInfoList sectionList = dir.entryInfoList(QDir::Filter::Dirs | QDir::NoDotAndDotDot);
    calculationCountBlock();
}

BlockIndex::BlockIndex(const BigNumber &recordsLimit)
    : BlockIndex() {
    this->recordsLimit = recordsLimit;
    qDebug() << "[BlockIndex] constructor: recordLimits - " << recordsLimit;
}

BlockIndex::BlockIndex(const QString &folderName) {
    qDebug() << "[BlockIndex] constructor: folder name - " << folderName;
}

BlockIndex::BlockIndex(const QString &folderName, const BigNumber &recordsLimit)
    : BlockIndex(folderName) {
    this->recordsLimit = recordsLimit;
}

int BlockIndex::addBlock(const Block &block) {
    int result = this->add(block.getIndex(), block.serialize());
    return result;
}

Block BlockIndex::getLastBlock() const {
    BigNumber id = this->lastSavedId;
    qDebug() << "[BlockIndex] Last saved id:" << this->lastSavedId;
    while (id >= getFirstSavedId()) {
        Block block = this->getBlockById(id);

        if (!block.isEmpty()) {
            // qDebug() << "[BlockIndex] getLastBlock:" << block.getIndex() << "is not empty, return this
            // block";
            return block;
        }

        // qDebug() << "[BlockIndex] getLastBlock:" << block.getIndex() << "is empty, can't return this
        // block";
        --id;
    }

    return Block();
}

Block BlockIndex::getLastRealBlock() const {
    BigNumber id = this->lastSavedId;
    qDebug() << "[BlockIndex] getLastBlock: last saved id -" << this->lastSavedId;
    while (id >= getFirstSavedId()) {
        Block block = this->getBlockById(id);
        if ((!block.isEmpty()) && (block.getType() != BlockType::Dummy)) {
            return block;
        }
        --id;
    }

    return Block();
}

GenesisBlock BlockIndex::getLastGenesisBlock() const {
    BigNumber id = this->lastSavedId;
    qDebug() << "[BlockIndex] getLastGenesisBlock: last saved id -" << this->lastSavedId;
    while (id >= getFirstSavedId()) {
        GenesisBlock block = this->getGenesisBlockById(id);
        if (!block.isEmpty()) {
            qDebug() << "[BlockIndex]" << block.getIndex() << "block is empty";
            return block;
        }
        --id;
    }
    return GenesisBlock();
}

GenesisBlock BlockIndex::getGenesisBlockById(const BigNumber &id) const {
    std::string serializedBlock = this->getById(id);
    Block bl(serializedBlock);
    if (!serializedBlock.empty() && bl.getType() == BlockType::Genesis) {
        return GenesisBlock(serializedBlock);
    }
    return GenesisBlock();
}

Block BlockIndex::getBlockById(const BigNumber &id) const {
    std::string serializedBlock = this->getById(id);
    Block block(serializedBlock);
    if (!block.isEmpty()) {
        if (block.getType() == BlockType::Data || block.getType() == BlockType::Dummy)
            return Block(serializedBlock);
        else if (block.getType() == BlockType::Genesis)
            return GenesisBlock(serializedBlock);
    } else {
        qDebug() << "[BlockIndex]" << id << "is not block";
    }
    return Block();
}

std::string BlockIndex::getBlockDataById(const BigNumber &id) const {
    std::string serializedBlock = this->getById(id);
    //    qDebug() << "BLOCK: " << serializedBlock;
    if (!serializedBlock.empty()) {
        return serializedBlock;
    } else {
        qDebug() << "[BlockIndex] is not block";
        return "";
    }
}

Block BlockIndex::getBlockByPosition(const BigNumber &position) const {
    BigNumber blockId = getFirstSavedId() + position;
    if (blockId <= this->lastSavedId) {
        Block block = this->getBlockById(blockId);
        return block;
    }
    return Block();
}

Block BlockIndex::getBlockByApprover(const BigNumber &approver) const {
    return getBlockByParam(approver, SearchEnum::BlockParam::Approver);
}

Block BlockIndex::getBlockByHash(const QByteArray &hash) const {
    return getBlockByParam(hash.toStdString(), SearchEnum::BlockParam::Hash);
}

Block BlockIndex::getBlockByData(const QByteArray &data) const {
    return getBlockByParam(data.toStdString(), SearchEnum::BlockParam::Data);
}

Block BlockIndex::getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const {
    if (param == SearchEnum::BlockParam::Id) {
        return getBlockById(id);
    }

    BigNumber lastBlockId = getLastSavedId();

    // iteration from the last to the first Block
    while (lastBlockId >= getFirstSavedId()) {
        Block lastBlock = getBlockById(lastBlockId);
        switch (param) {
        case SearchEnum::BlockParam::Approver: {
            if (BigNumber(lastBlock.getApprover().toStdString()) == id)
                return lastBlock;
            break;
        }
        case SearchEnum::BlockParam::Data: {
            if (lastBlock.getData() == id.toStdString())
                return lastBlock;
            break;
        }
        case SearchEnum::BlockParam::Hash: {
            if (lastBlock.getHash() == id.toStdString())
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

Block BlockIndex::getLastRealBlockById() {
    BigNumber id = this->lastSavedId;
    while (id >= getFirstSavedId()) {
        Block block = this->getBlockById(id);
        if (!block.isEmpty() && block.getType() != BlockType::Dummy) {
            return block;
        }
        --id;
    }
    return Block();
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByHash(const QByteArray &hash, const QByteArray &token) const {
    return getLastTxByParam(hash.toStdString(), SearchEnum::TxParam::Hash, token);
}

std::pair<Transaction, QByteArray> BlockIndex::getLastTxByData(const std::string &data) const {
    return getLastTxByParam(data, SearchEnum::TxParam::Data, "token");
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxBySender(const BigNumber &id, const QByteArray &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserSender, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByReceiver(const BigNumber &id, const QByteArray &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserReceiver, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxBySenderOrReceiver(const BigNumber &id, const QByteArray &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserSenderOrReceiver, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxBySenderOrReceiverAndToken(const BigNumber &id, const QByteArray &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserSenderOrReceiverOrToken, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByApprover(const BigNumber &id, const QByteArray &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserApprover, token);
}

QList<Transaction>
BlockIndex::getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from, int count, BigNumber token)
    const {
    return getTxsByParamInRow(id, SearchEnum::TxParam::UserSenderOrReceiver, from, count, token);
}

// QList<Transaction> BlockIndex::getRecentTxList(const BigNumber &last, const BigNumber &first) const {
//    QList<Transaction> txList;

//}

std::pair<Transaction, QByteArray> BlockIndex::getLastTxByParam(
    const std::string &id,
    SearchEnum::TxParam param,
    const QByteArray &token) const {
    BigNumber records = getRecords();
    ActorId tokenActor = token.toStdString();

    if (records == 0) {
        qDebug() << "[BlockIndex] There no tx's in block index";
        return { Transaction(), "-1" };
    }

    BigNumber lastBlockId = getLastSavedId();

    // iterating from last to first block
    while (lastBlockId >= getFirstSavedId()) {
        Block lastBlock = getBlockById(lastBlockId);
        auto txs = lastBlock.extractTransactions();

        for (const Transaction &tx : txs) {
            if (tx.getToken() != tokenActor)
                continue;
            switch (param) {
            case SearchEnum::TxParam::UserSenderOrReceiverOrToken: {
                if (tx.getSender().toStdString() == id || tx.getReceiver().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserSender: {
                if (tx.getSender().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserReceiver: {
                if (tx.getReceiver().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver: {
                if (tx.getSender().toStdString() == id || tx.getReceiver().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserApprover: {
                if (tx.getApprover().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::Hash: {
                if (tx.getHash() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::Data: {
                if (lastBlock.getType() == BlockType::Genesis)
                    return { Transaction(), "-1" };
                if (tx.getData() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            default: {
                break;
            }
            }
        }
        --lastBlockId;
    }
    return { Transaction(), "-1" };
}

QList<Transaction> BlockIndex::getTxsByParamInRow(
    const BigNumber &id,
    SearchEnum::TxParam param,
    BigNumber from,
    int count,
    BigNumber token) const {
    QList<Transaction> currentTxs;
    BigNumber records = getRecords();

    if (records == 0) {
        qDebug() << "[BlockIndex] There no tx's in block index";
        return currentTxs;
    }

    BigNumber lastBlockId = from == -1 ? getLastSavedId() : from;
    int currentCount = 0;

    while (lastBlockId >= getFirstSavedId()) {
        // qDebug() << count << currentCount << (count < currentCount);

        if (count < currentCount)
            break;

        Block lastBlock = getBlockById(lastBlockId);
        auto txs = lastBlock.extractTransactions();

        for (const Transaction &tx : txs) {
            if (BigNumber(tx.getToken().toStdString()) != token)
                continue;
            switch (param) {
            case SearchEnum::TxParam::UserSender: {
                if (BigNumber(tx.getSender().toStdString()) == id
                    && BigNumber(tx.getToken().toStdString()) == token) {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserReceiver: {
                if (BigNumber(tx.getReceiver().toStdString()) == id
                    && BigNumber(tx.getToken().toStdString()) == token) {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver: {
                if ((BigNumber(tx.getSender().toStdString()) == id
                     || BigNumber(tx.getReceiver().toStdString()) == id)
                    && BigNumber(tx.getToken().toStdString()) == token) {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserApprover: {
                if (BigNumber(tx.getApprover().toStdString()) == id
                    && BigNumber(tx.getToken().toStdString()) == token) {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::Hash: {
                if (BigNumber(tx.getHash()) == id && BigNumber(tx.getToken().toStdString()) == token) {
                    currentTxs << tx;
                    ++currentCount;
                }
                break;
            }
            default: {
            }
            }
        }

        --lastBlockId;
    }

    // qDebug() << "currentTxs" << currentTxs.length();

    return currentTxs;
}

QString BlockIndex::buildFilePath(const BigNumber &id) const {
    BigNumber section = this->calcSection(id);
    QString pathToFolder = getFolderPath() + "/" + section.toByteArray();

    QDir dir(pathToFolder);
    if (!dir.exists()) {
        qDebug() << "[BlockIndex] Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + id.toByteArray();
}

BigNumberFloat BlockIndex::calculateCirculativeBalance() const {
    BigNumberFloat circulativeBalance = 0;
    bool isGenesisBlockFounde = false;
    auto lastId = lastSavedId;
    while (!isGenesisBlockFounde) {
        const auto block = getBlockById(lastId);
        if (block.getType() == BlockType::Genesis) {
            isGenesisBlockFounde = true;
        } else {
            circulativeBalance += calculateCirculativeBalanceBlock(block);
            lastId--;
        }
    }
    return circulativeBalance;
}

BigNumberFloat BlockIndex::calculateCirculativeBalanceBlock(const Block &block) const {
    BigNumberFloat circulativeBalanceBlock(0);

    const auto allTx = block.extractTransactions();
    if (allTx.empty())
        return BigNumberFloat(0);

    for (int numberTx = 0; numberTx < allTx.size(); numberTx++) {
        if (allTx[numberTx].isRewardTransaction()) {
            circulativeBalanceBlock += allTx[numberTx].getAmount();
        }
    }
    return circulativeBalanceBlock;
}

BigNumberFloat BlockIndex::calculateCirculativeBalanceLastGenesisBlock() const {
    BigNumberFloat circulativeBalanceGenesisBlock(0);
    const auto genesisBlock = getLastGenesisBlock();

    const auto dataRows = genesisBlock.dataRows();
    for (int numberRow = 0; numberRow < dataRows.size(); numberRow++) {
        const GenesisDataRow dataRow = dataRows[numberRow];
        if (dataRow.type == DataStorage::typeDataRow::UNIVERSAL && dataRow.token == ActorId()) {
            circulativeBalanceGenesisBlock += dataRow.state;
        }
    }
    return circulativeBalanceGenesisBlock;
}

void BlockIndex::calculationCountBlock() {
    BigNumber id = this->lastSavedId;
    qDebug() << "[BlockIndex] getLastBlock: last saved id:" << this->lastSavedId;
    while (id >= firstSavedId) {
        Block block = this->getBlockById(id);
        if (!block.isEmpty()) {
            if (block.getType() == BlockType::Data) {
                qDebug() << "[BlockIndex] Block by index" << block.getIndex() << " is real";
                realBlockRecords++;
                qDebug() << "[BlockIndex] Count real blocks:" << realBlockRecords;
            }
            countTransactions += block.extractTransactions().size();
            records++;
        }
        --id;
    }
    qDebug() << "[BlockIndex] Count records:" << records << "";
}

int BlockIndex::add(const BigNumber &id, const std::string &_data) {
    QString path = buildFilePath(id);
    QFile file(path);

    qDebug() << "[BlockIndex] Saving the file:" << path;

    if (file.exists()) {
        qDebug() << "[BlockIndex] Can't save the file" << path << "(file already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }

    if (recordLimitIsReached()) {
        if (this->firstSavedId != 0) {
            this->removeById(getBlockById(getFirstSavedId()));
            this->firstSavedId++; // todo: check!
        }
    }

    DBConnector DB(path.toStdString());
    if (DB.open()) {
        Block bl(_data);
        if (bl.getType() == BlockType::Genesis) {
            GenesisBlock block(_data);
            DB.createTable(Config::DataStorage::GenesisBlockTableCreate);
            DB.createTable(Config::DataStorage::RowGenesisBlockTableCreate);
            DB.createTable(Config::DataStorage::SignBlockTableCreate);
            DBRow row;

            row.insert({ "type", block.getTypeStr() });
            row.insert({ "id", block.getIndex().toStdString() });
            row.insert({ "date", QByteArray::number(block.getDate()).toStdString() });
            row.insert({ "data", "" });
            row.insert({ "prevHash", block.getPrevHash() });
            row.insert({ "hash", block.getHash() });
            row.insert({ "prevGenHash", block.getPrevGenHash() });
            DB.insert(Config::DataStorage::GenesisBlockTable, row);

            auto rows = block.dataRows();
            for (const auto &tmp : std::as_const(rows)) {
                DBRow rowRow;
                rowRow.insert({ "actorId", tmp.actorId.toStdString() });
                rowRow.insert({ "state", tmp.state.toStdString() });
                rowRow.insert({ "token", tmp.token.toStdString() });
                rowRow.insert({ "type", QByteArray::number(tmp.type).toStdString() });
                DB.insert(Config::DataStorage::RowGenesisBlockTable, rowRow);
            }
            QByteArrayList listSign = block.getListSignatures();
            for (int i = 0; i < listSign.size(); i += 3) {
                DBRow rowRow;
                rowRow.insert({ "actorId", listSign[i].toStdString() });
                rowRow.insert({ "signature", listSign[i + 1].toStdString() });
                rowRow.insert({ "isApprove", listSign[i + 2].toStdString() });
                DB.insert(Config::DataStorage::SignTable, rowRow);
            }
        } else {
            Block block(_data);
            DB.createTable(Config::DataStorage::BlockTableCreate);
            DB.createTable(Config::DataStorage::TxBlockTableCreate);
            DB.createTable(Config::DataStorage::SignBlockTableCreate);
            DBRow row;

            row.insert({ "type", block.getTypeStr() });
            row.insert({ "id", block.getIndex().toStdString() });
            row.insert({ "date", QByteArray::number(block.getDate()).toStdString() });
            row.insert({ "data", "" });
            row.insert({ "prevHash", block.getPrevHash() });
            row.insert({ "hash", block.getHash() });
            DB.insert(Config::DataStorage::BlockTable, row);

            auto rows = block.extractTransactions();
            for (const auto &tmp : rows) {
                DBRow rowRow;
                rowRow.insert({ "sender", tmp.getSender().toByteArray().toStdString() });
                rowRow.insert({ "receiver", tmp.getReceiver().toByteArray().toStdString() });
                rowRow.insert({ "amount", tmp.getAmount().toStdString() });
                rowRow.insert({ "date", QByteArray::number(tmp.getDate()).toStdString() });
                rowRow.insert({ "token", tmp.getToken().toByteArray().toStdString() });
                rowRow.insert({ "data", tmp.getData() });
                rowRow.insert({ "prevBlock", tmp.getPrevBlock().toStdString() });
                rowRow.insert({ "gas", QByteArray::number(tmp.getGas()).toStdString() });
                rowRow.insert({ "hop", QByteArray::number(tmp.getHop()).toStdString() });
                rowRow.insert({ "hash", tmp.getHash() });
                rowRow.insert({ "approver", tmp.getApprover().toByteArray().toStdString() });
                rowRow.insert({ "signature", tmp.getSignature() });
                if (tmp.getProducer().isEmpty())
                    rowRow.insert({ "producer", "0" });
                else
                    rowRow.insert({ "producer", tmp.getProducer().toByteArray().toStdString() });
                bool txInserted = DB.insert(Config::DataStorage::TxBlockTable, rowRow);
                if (txInserted)
                    countTransactions++;
            }
            QByteArrayList listSign = block.getListSignatures();
            for (int i = 0; i < listSign.size(); i += 3) {
                DBRow rowRow;
                rowRow.insert({ "actorId", listSign[i].toStdString() });
                rowRow.insert({ "signature", listSign[i + 1].toStdString() });
                rowRow.insert({ "isApprove", listSign[i + 2].toStdString() });
                DB.insert(Config::DataStorage::SignTable, rowRow);
            }

            if (block.getType() == BlockType::Data)
                realBlockRecords++;
        }
        this->records = records + 1;

        // updating last saved id is a regular operation
        if (id > this->lastSavedId) {
            this->lastSavedId = id;
        }

        // but updating the first saved id is rarely (should be logged)
        if (id < this->firstSavedId || firstSavedId.isEmpty()) {
            qDebug() << "[BlockIndex] First saved id is updated from" << firstSavedId << "to" << id;
            this->firstSavedId = id;
        }

        return 0;
    }
    qDebug() << "[BlockIndex] Can't save the file" << path << "(file is not opened)";
    return Errors::FILE_IS_NOT_OPENED;
}

bool BlockIndex::hasRecordLimit() const {
    return !this->recordsLimit.isEmpty();
}

bool BlockIndex::recordLimitIsReached() const {
    return this->hasRecordLimit() && (this->records >= this->recordsLimit);
}
int BlockIndex::removeById(const BigNumber &id) {
    return removeById(getBlockById(id));
}

int BlockIndex::removeById(const Block &block) {
    //    block.getIndex(), block.getType()
    auto id = block.getIndex();
    auto typeBlock = block.getType();
    auto countTxInBlock = block.extractTransactions().size();
    qDebug() << "[BlockIndex] Removing record with id" << id.toByteArray();
    if (id < firstSavedId) {
        removeAll();
    }
    qDebug() << "[BlockIndex]" << lastSavedId << "(last saved id)" << id << "(id to remove)";

    BigNumber currentIdToRemove = id;

    while (currentIdToRemove <= lastSavedId) {
        QString pathToFile = buildFilePath(currentIdToRemove);
        qDebug() << "[BlockIndex] To remove:" << pathToFile;
        QFile file(pathToFile);
        if (file.exists() && !file.isOpen()) {
            bool isRemoved = file.remove();
            if (isRemoved) {
                this->records--;
            }

            if (isRemoved && typeBlock == BlockType::Data) {
                this->realBlockRecords--;
                countTransactions -= countTxInBlock;
            }
        }
        currentIdToRemove++;
    }

    this->lastSavedId = BigNumber(id) - 1;
    return 0;
}

void BlockIndex::removeDummyBlocks(const BigNumber &id) {
    qDebug() << "[BlockIndex] remove dummy blocks";
    bool isNotDummyBlock = false;
    auto lastId = lastSavedId;
    while (!isNotDummyBlock) {
        const auto block = getBlockById(lastId);
        if (block.getType() != BlockType::Dummy) {
            isNotDummyBlock = true;
        } else {
            removeById(block);
            lastId--;
        }
    }
}

void BlockIndex::removeAll() {
    QString folderPath = this->getFolderPath();
    qDebug() << "Clearing file index:" << folderPath;

    QDir folder(folderPath);
    const auto folders =
        folder.entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name);
    for (const QString &section : std::as_const(folders)) {
        QDir dir(folderPath + QString("/") + section);
        dir.removeRecursively();
    }

    // update state
    this->records = 0;
    this->firstSavedId = 0;
    this->lastSavedId = 0;
    this->realBlockRecords = 0;
    this->countTransactions = 0;
}

QString BlockIndex::getFolderPath() const {
    return DataStorage::BLOCKCHAIN_INDEX + "/" + this->getFolderName();
}

QString BlockIndex::getFolderName() const {
    return this->folderName;
}

BigNumber BlockIndex::getFirstSavedId() const {
    return this->firstSavedId;
}

BigNumber BlockIndex::calcSection(BigNumber id) const {
    return id / BigNumber(sectionSize);
}

BigNumber BlockIndex::getLastSavedId() const {
    return this->lastSavedId;
}

BigNumber BlockIndex::getRecords() const {
    return this->records;
}

BigNumber BlockIndex::getCountRealBlocks() const {
    return this->realBlockRecords;
}

int BlockIndex::getCountTransactionsInBlocks() const {
    return countTransactions;
}

BigNumber BlockIndex::getIndexBlockByLastFarmingTx() const {
    if (getRecords().isEmpty())
        return BigNumber(-1);

    BigNumber lastBlockId = getLastSavedId();

    while (lastBlockId >= getFirstSavedId()) {
        Block lastBlock = getBlockById(lastBlockId);
        auto txs = lastBlock.extractTransactions();

        for (const Transaction &tx : txs) {
            if (tx.isFarmingTransaction())
                return lastBlockId;
        }
        --lastBlockId;
    }
    return BigNumber(-1);
}

std::list<FarmingTransactionData> BlockIndex::getAllLockedFarmingTransactions() const {
    std::list<FarmingTransactionData> farmingsTxs;
    if (getRecords().isEmpty())
        return farmingsTxs;

    BigNumber lastBlockId = getLastSavedId();
    BigNumber farmingBlockIndex = BigNumber(302400);
    BigNumber toBlockIndex =
        (lastBlockId - farmingBlockIndex) > 0 ? (lastBlockId - farmingBlockIndex) : getFirstSavedId();

    while (lastBlockId >= getFirstSavedId() || lastBlockId >= toBlockIndex) {
        Block lastBlock = getBlockById(lastBlockId);
        auto txs = lastBlock.extractTransactions();

        for (const Transaction &tx : txs) {
            if (tx.isFarmingTransaction())
                farmingsTxs.push_front(
                    FarmingTransactionData { .index = farmingBlockIndex - lastBlockId, .transaction = tx });
        }
        --lastBlockId;
    }

    return farmingsTxs;
}

std::string BlockIndex::getByIdUnsafe(const BigNumber &id) const {
    std::string path = buildFilePath(id).toStdString();

    if (!std::filesystem::exists(path)) {
        qDebug() << "Can't get the file" << path << "(file is not exits)";
        return "";
    }

    DBConnector db(path);
    db.open();

    if (db.tableNames().empty()) {
        return "";
    }

    bool isGenesis = db.tableNames()[0] == "GenesisBlock";
    const std::string blockTable =
        isGenesis ? Config::DataStorage::GenesisBlockTable : Config::DataStorage::BlockTable;

    std::vector<DBRow> res = db.selectAll(blockTable);
    if (res.empty()) {
        qDebug() << "[BlockIndex] Can't get the file" << path << "(file is empty)";
        return "";
    }

    BigNumber blockId = BigNumber(res[0].at("id"));
    long long date = std::stoll(res[0].at("date"));

    if (id != blockId)
        qFatal("getById: Why?");

    std::vector<DBRow> dbSigns = db.select("SELECT * FROM " + Config::DataStorage::SignTable + ";");
    std::vector<Approvers> signatures;

    for (const auto &dbSign : dbSigns) {
        auto block_sign = Approvers { .actorId = dbSign.at("actorId"),
                                      .sign = dbSign.at("signature"),
                                      .isApprove = boost::lexical_cast<bool>(dbSign.at("isApprove")) };
        signatures.push_back(block_sign);
    }

    if (isGenesis) {
        std::string prevGenHash = std::move(res[0].at("prevGenHash"));

        std::vector<DBRow> rows = db.selectAll(Config::DataStorage::RowGenesisBlockTable);
        std::vector<GenesisDataRow> dataRows;
        for (const auto &row : rows) {
            GenesisDataRow dRow;
            dRow.type = DataStorage::typeDataRow(QByteArray(row.at("type").c_str()).toInt());
            dRow.state = BigNumber(row.at("state"));
            dRow.token = row.at("token");
            dRow.actorId = row.at("actorId");
            dataRows.push_back(dRow);
        }

        auto block = GenesisBlock(
            std::move(res[0].at("type")),
            std::move(res[0].at("data")),
            blockId,
            date,
            std::move(res[0].at("prevHash")),
            std::move(res[0].at("hash")),
            std::move(prevGenHash),
            std::move(signatures),
            std::move(dataRows));

        return block.serialize();
    } else {
        std::vector<DBRow> rows = db.selectAll(Config::DataStorage::TxBlockTable);
        std::vector<Transaction> transactions;

        for (const auto &tmp : rows) {
            Transaction tx;
            tx.setSender(ActorId(tmp.at("sender")));
            tx.setReceiver(ActorId(tmp.at("receiver")));
            tx.setAmount(BigNumber(tmp.at("amount")));
            tx.setDate(std::stoll(tmp.at("date")));
            tx.setData(tmp.at("data"));
            tx.setToken(ActorId(tmp.at("token")));
            tx.setPrevBlock(BigNumber(tmp.at("prevBlock")));
            tx.setGas(std::stoi(tmp.at("gas")));
            tx.setHop(std::stoi(tmp.at("hop")));
            tx.setHash(tmp.at("hash").c_str());
            tx.setApprover(ActorId(tmp.at("approver")));
            tx.setSignature(tmp.at("signature").c_str());
            tx.setProducer(ActorId(tmp.at("producer")));

            if (!tx.isEmpty() || tx.isBurn()) // TODO: ?
                transactions.push_back(tx);
        }

        auto block = Block(
            std::move(res[0].at("type")),
            std::move(res[0].at("data")),
            blockId,
            date,
            std::move(res[0].at("prevHash")),
            std::move(res[0].at("hash")),
            std::move(signatures),
            std::move(transactions));

        return block.serialize();
    }
}

std::string BlockIndex::getById(const BigNumber &id) const {
    try {
        auto res = getByIdUnsafe(id);
        return res;
    } catch (const std::exception &e) {
        qFatal("[BlockIndex] Wrong getById: %s", e.what());
    }

    return "";
}

BigNumber BlockIndex::loadFirstId() {
    BigNumber firstSavedId = loadFileFromSection(
        [](const QStringList &folders) {
            return folders[0];
        },
        [](const QStringList &files) {
            return files[0];
        });

    if (!firstSavedId.isEmpty()) {
        qDebug() << "[BlockIndex] loadFirsId: Loaded first saved id:" << firstSavedId;
    } else {
        qDebug() << "[BlockIndex] loadFirsId: First saved id is not loaded";
    }

    return firstSavedId;
}

BigNumber BlockIndex::loadFileFromSection(
    std::function<QString(const QStringList &folders)> getFolder,
    std::function<QString(const QStringList &files)> getFile) {
    auto asBigNumComparator = [](const QString &file1, const QString &file2) {
        return BigNumber(file1.toStdString()) < BigNumber(file2.toStdString());
    };

    QDir folder(getFolderPath());

    // sections
    qDebug() << "[BlockIndex] loadFileFromSection():" << folder.path();
    QStringList list = folder.entryList(QDir::Filter::Dirs | QDir::Filter::NoDotAndDotDot);
    if (list.isEmpty()) {
        qDebug() << "[BlockIndex] loadFileFromSection(): folder.entryList: empty";
        return BigNumber();
    }
    std::sort(list.begin(), list.end(), asBigNumComparator);
    folder.cd(getFolder(list)); // go to section

    // files in sections
    qDebug() << "[BlockIndex] loadFileFromSection():" << folder.path();
    list = folder.entryList(QDir::Filter::Files | QDir::Filter::NoDotAndDotDot);
    if (list.isEmpty()) {
        qDebug() << "[BlockIndex] loadFileFromSection(): folder.entryList->folder.entryList: empty";
        return BigNumber();
    }
    std::sort(list.begin(), list.end(), asBigNumComparator);

    qDebug() << "[BlockIndex] loadFileFromSection(): lastId -"
             << (list.isEmpty() ? BigNumber() : BigNumber(getFile(list).toStdString()));
    return list.isEmpty() ? BigNumber() : BigNumber(getFile(list).toStdString());
}

BigNumber BlockIndex::loadLastId() {
    BigNumber lastSavedId = loadFileFromSection(
        [](const QStringList &folders) {
            return folders.last();
        },
        [](const QStringList &files) {
            return files.last();
        });

    if (!lastSavedId.isEmpty()) {
        qDebug() << "[BlockIndex] Loaded last saved id:" << lastSavedId;
    } else {
        qDebug() << "[BlockIndex] Last saved id is not loaded";
    }
    return lastSavedId;
}
