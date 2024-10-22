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

#include "datastorage/blockchain.h"

BlockIndex::BlockIndex() {
    this->folderName  = DataStorage::BLOCK_INDEX_FOLDER_NAME;
    this->sectionSize = Config::DataStorage::SECTION_SIZE;
    firstSavedId      = loadFirstId();
    lastSavedId       = loadLastId();
    QDir          dir(DataStorage::BLOCKCHAIN_INDEX + '/' + folderName);
    QFileInfoList sectionList = dir.entryInfoList(QDir::Filter::Dirs | QDir::NoDotAndDotDot);

    removeDummyBlocks();
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

void BlockIndex::setBlockCompress(bool newBlockCompress) {
    m_blockCompress = newBlockCompress;
}

std::expected<BlockVariant, BlockError> BlockIndex::addBlock(const BlockVariant &block) {
    auto result = this->add(block.getIndex(), block);
    return result;
}

std::expected<BlockVariant, BlockError> BlockIndex::getLastBlock() const {
    BigNumber id = this->lastSavedId;

    while (id >= getFirstSavedId()) {
        auto block = this->getBlockById(id);

        if (block.has_value() && !block->isEmpty()) {
            return block;
        }

        --id;
    }

    return std::unexpected(BlockError::NotExists);
}

std::expected<BlockVariant, BlockError> BlockIndex::getLastRealBlock() const {
    BigNumber id = this->lastSavedId;
    // qDebug() << "[BlockIndex] Last real block:" << this->lastSavedId;
    while (id >= getFirstSavedId()) {
        auto block = this->getBlockById(id);

        if (block.has_value() && block->getType() != BlockType::Dummy && !block->isEmpty()) {
            return block;
        }

        --id;
    }

    return std::unexpected(BlockError::NotExists);
}

std::expected<BlockVariant, BlockError> BlockIndex::getLastGenesisBlock(const BigNumber &from) const {
    BigNumber id = Blockchain::lastGenesisIdFor(from >= 0 ? from : this->lastSavedId);

    while (id >= getFirstSavedId()) {
        auto block = this->getGenesisBlockById(id);

        if (block.has_value() && !block->isEmpty()) {
            // qDebug() << "[BlockIndex]" << block->getIndex() << "block found";
            return block.value();
        }

        id -= Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
    }

    qFatal("No genesis?");
    return std::unexpected(BlockError::NoGenesis); // BlockError::BlockNotExists?
}

std::expected<BlockVariant, BlockError> BlockIndex::getGenesisBlockById(const BigNumber &id) const {
    auto block = this->getById(id);

    if (block.has_value() && !block->isEmpty() && block->isGenesisBlock()) {
        return block;
    }

    return std::unexpected(BlockError::NoGenesis);
}

std::expected<BlockVariant, BlockError> BlockIndex::getBlockById(const BigNumber &id) const {
    if (id < 0) {
        qFatal("getBlockById < 0");
    }

    auto block = this->getById(id);
    if (block.has_value() && !block->isEmpty()) {
        return block;
    }

    // qDebug() << "[BlockIndex]" << id << "not exists, maybe past dummy?";
    return std::unexpected(BlockError::NotExists);
}

std::expected<BlockVariant, BlockError> BlockIndex::getBlockByPosition(const BigNumber &position) const {
    BigNumber blockId = getFirstSavedId() + position;
    if (blockId <= this->lastSavedId) {
        auto block = this->getBlockById(blockId);
        return block;
    }

    return std::unexpected(BlockError::NotExists);
}

std::expected<BlockVariant, BlockError> BlockIndex::getBlockByHash(const QByteArray &hash) const {
    return getBlockByParam(hash.toStdString(), SearchEnum::BlockParam::Hash);
}

std::expected<BlockVariant, BlockError> BlockIndex::getBlockByData(const QByteArray &data) const {
    return getBlockByParam(data.toStdString(), SearchEnum::BlockParam::Data);
}

std::expected<BlockVariant, BlockError>
BlockIndex::getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const {
    if (param == SearchEnum::BlockParam::Id) {
        return getBlockById(id);
    }

    BigNumber lastBlockId = getLastSavedId();

    // iteration from the last to the first Block
    while (lastBlockId >= getFirstSavedId()) {
        auto lastBlock = getBlockById(lastBlockId);

        if (!lastBlock.has_value())
            return std::unexpected(BlockError::NotExists);
        if (lastBlock->isEmpty())
            return std::unexpected(BlockError::NotExists);

        switch (param) {
        case SearchEnum::BlockParam::Data: {
            if (lastBlock->dataService().contains(id.toStdString()))
                return lastBlock;
            break;
        }
        case SearchEnum::BlockParam::Hash: {
            if (lastBlock->getHash() == id.toStdString())
                return lastBlock;
            break;
        }
        default:
            break;
        }
        --lastBlockId;
    }
    return std::unexpected(BlockError::NotExists);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByHash(const QByteArray &hash, const ActorId &token) const {
    return getLastTxByParam(hash.toStdString(), SearchEnum::TxParam::Hash, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByData(const std::string &data, const ActorId &token) const {
    return getLastTxByParam(data, SearchEnum::TxParam::Data, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxBySender(const BigNumber &id, const ActorId &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserSender, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByReceiver(const BigNumber &id, const ActorId &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserReceiver, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxBySenderOrReceiver(const BigNumber &id, const ActorId &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserSenderOrReceiver, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxBySenderOrReceiverAndToken(const BigNumber &id, const ActorId &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserSenderOrReceiverOrToken, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByApprover(const BigNumber &id, const ActorId &token) const {
    return getLastTxByParam(id.toStdString(), SearchEnum::TxParam::UserApprover, token);
}

std::set<Transaction> BlockIndex::getTxsBySenderOrReceiverInRow(
    const BigNumber &id,
    BigNumber        from,
    int              count,
    const ActorId   &token) const {
    return getTxsByParamInRow(id, SearchEnum::TxParam::UserSenderOrReceiver, from, count, token);
}

std::pair<Transaction, QByteArray>
BlockIndex::getLastTxByParam(const std::string &id, SearchEnum::TxParam param, const ActorId &token) const {
    BigNumber records    = getRecords();
    ActorId   tokenActor = token.toStdString();

    if (records == 0) {
        qDebug() << "[BlockIndex] There no tx's in block index";
        return { Transaction(), "-1" };
    }

    BigNumber lastBlockId = getLastSavedId();

    // iterating from last to first block
    while (lastBlockId >= getFirstSavedId()) {
        auto lastBlock = getBlockById(lastBlockId);
        if (!lastBlock.has_value()) {
            --lastBlockId;
            continue;
        }
        if (lastBlock->isGenesisBlock() || lastBlock->isEmpty()) {
            --lastBlockId;
            continue;
        }

        auto txs = lastBlock->transactions();

        for (const Transaction &tx : txs) {
            if (tx.token() != tokenActor)
                continue;
            switch (param) {
            case SearchEnum::TxParam::UserSenderOrReceiverOrToken: {
                if (tx.sender().toStdString() == id || tx.receiver().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserSender: {
                if (tx.sender().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserReceiver: {
                if (tx.receiver().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver: {
                if (tx.sender().toStdString() == id || tx.receiver().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::UserApprover: {
                if (tx.approver().toStdString() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::Hash: {
                if (tx.hash() == id)
                    return { tx, lastBlockId.toByteArray() };
                break;
            }
            case SearchEnum::TxParam::Data: {
                if (lastBlock->getType() == BlockType::Genesis)
                    return { Transaction(), "-1" };
                if (tx.data() == id)
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

std::set<Transaction> BlockIndex::getTxsByParamInRow(
    const BigNumber    &id,
    SearchEnum::TxParam param,
    BigNumber           from,
    int                 count,
    ActorId             token) const {
    std::set<Transaction> currentTxs;
    BigNumber             records = getRecords();

    if (records == 0) {
        qDebug() << "[BlockIndex] There no tx's in block index";
        return currentTxs;
    }

    BigNumber lastBlockId  = from == -1 ? getLastSavedId() : from;
    int       currentCount = 0;

    while (lastBlockId >= getFirstSavedId()) {
        // qDebug() << count << currentCount << (count < currentCount);

        if (count < currentCount)
            break;

        auto lastBlock = getBlockById(lastBlockId);

        if (!lastBlock.has_value() || (lastBlock.has_value() && lastBlock->isGenesisBlock())) {
            --lastBlockId;
            continue;
        }

        auto txs = lastBlock->transactions();

        for (const Transaction &tx : txs) {
            if (tx.token() != token)
                continue;
            switch (param) {
            case SearchEnum::TxParam::UserSender: {
                if (BigNumber(tx.sender().toStdString()) == id && tx.token() == token) {
                    currentTxs.insert(tx);
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserReceiver: {
                if (BigNumber(tx.receiver().toStdString()) == id && tx.token() == token) {
                    currentTxs.insert(tx);
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver: {
                if ((BigNumber(tx.sender().toStdString()) == id
                     || BigNumber(tx.receiver().toStdString()) == id)
                    && tx.token() == token) {
                    currentTxs.insert(tx);
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::UserApprover: {
                if (BigNumber(tx.approver().toStdString()) == id && tx.token() == token) {
                    currentTxs.insert(tx);
                    ++currentCount;
                }
                break;
            }
            case SearchEnum::TxParam::Hash: {
                if (BigNumber(tx.hash()) == id && tx.token() == token) {
                    currentTxs.insert(tx);
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
    BigNumber section      = this->calcSection(id);
    QString   pathToFolder = getFolderPath() + "/" + section.toByteArray();

    QDir dir(pathToFolder);
    if (!dir.exists()) {
        qDebug() << "[BlockIndex] Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + id.toByteArray();
}

void BlockIndex::calculationCountBlock() {
    BigNumber id = this->lastSavedId;
    qDebug() << "[BlockIndex] getLastBlock: last saved id:" << this->lastSavedId;

    while (id >= firstSavedId) {
        auto block = this->getBlockById(id);

        if (block.has_value() && !block->isEmpty()) {
            if (block->getType() == BlockType::Data) {
                // qDebug() << "[BlockIndex] Block by index" << block.getIndex() << "is real";
                realBlockRecords++;
                // qDebug() << "[BlockIndex] Count real blocks:" << realBlockRecords;
            }

            if (block->isBlock() && block->getType() == BlockType::Dummy) {
                countTransactions += block->transactions().size();
            }

            records++;
        }

        --id;
    }

    qDebug() << "[BlockIndex] Count records:" << records << "";
}

std::expected<BlockVariant, BlockError> BlockIndex::add(const BigNumber &id, const BlockVariant &newBlock) {
    QString path = buildFilePath(id);

    QFile file(path);
    if (file.exists()) {
        auto block = getBlockById(id);

        if (block.has_value() && !block->isEmpty()) {
            return std::unexpected(BlockError::AlreadyExists);
        }

        file.remove();
    }

    if (recordLimitIsReached()) {
        // start from genesis, remove first 100 blocks?
        // if (this->firstSavedId != 0) {
        //     this->removeById(getBlockById(getFirstSavedId()));
        //     this->firstSavedId++; // todo: check!
        // }
    }

    DBConnector db(
        path.toStdString(),
        m_blockCompress ? DBConnectorType::Compressed : DBConnectorType::Regular);

    auto bl = newBlock;
    if (!db.open()) {
        return std::unexpected(BlockError::Invalid);
    }

    if (newBlock.isGenesisBlock()) {
        GenesisBlock block = bl.getGenesisBlock()->get();

        db.createTable(Config::DataStorage::GenesisBlockTableCreate);
        db.createTable(Config::DataStorage::RowGenesisBlockTableCreate);
        db.createTable(Config::DataStorage::SignBlockTableCreate);

        DBRow row;
        row.insert({ "type", block.getTypeStr() });
        row.insert({ "id", block.getIndex().toStdString() });
        row.insert({ "date", QByteArray::number(block.getDate()).toStdString() });
        row.insert({ "data", block.getDataMessagePack() });
        row.insert({ "prevHash", block.getPrevHash() });
        row.insert({ "hash", block.getHash() });
        row.insert({ "prevGenHash", block.getPrevGenHash() });
        db.insert(Config::DataStorage::GenesisBlockTable, row);

        auto rows = block.dataRows();
        for (const auto &[key, row] : std::as_const(rows)) {
            const auto &[actorId, tokenId] = key;
            DBRow rowRow;
            rowRow.insert({ "actorId", actorId.toStdString() });
            rowRow.insert({ "state", row.state.toStdString() });
            rowRow.insert({ "token", tokenId.toStdString() });
            rowRow.insert({ "type", QByteArray::number(row.type).toStdString() });
            db.insert(Config::DataStorage::RowGenesisBlockTable, rowRow);
        }

        auto signatures = block.signatures();
        for (const auto &[actorId, sign] : std::as_const(signatures)) {
            DBRow rowRow;
            rowRow.insert({ "actorId", actorId.toStdString() });
            rowRow.insert({ "signature", sign });
            rowRow.insert({ "isApprove", "1" /*std::to_string(sign.isApprove)*/ });
            db.insert(Config::DataStorage::SignTable, rowRow);
        }

        realBlockRecords++;
        this->records = records + 1;

        if (id > this->lastSavedId) {
            this->lastSavedId = id;
        }

        if (id < this->firstSavedId || firstSavedId.isEmpty()) {
            qDebug() << "[BlockIndex] First saved id is updated from" << firstSavedId << "to" << id;
            this->firstSavedId = id;
        }

        return BlockVariant(block);
    } else {
        Block block = bl.getBlock()->get();

        db.createTable(Config::DataStorage::BlockTableCreate);
        db.createTable(Config::DataStorage::TxBlockTableCreate);
        db.createTable(Config::DataStorage::SignBlockTableCreate);
        DBRow row;

        row.insert({ "type", block.getTypeStr() });
        row.insert({ "id", block.getIndex().toStdString() });
        row.insert({ "date", QByteArray::number(block.getDate()).toStdString() });
        row.insert({ "data", block.getDataMessagePack() });
        row.insert({ "prevHash", block.getPrevHash() });
        row.insert({ "hash", block.getHash() });
        db.insert(Config::DataStorage::BlockTable, row);

        auto rows = block.transactions();
        for (const auto &tmp : std::as_const(rows)) {
            DBRow rowRow;
            rowRow.insert({ "type", std::to_string(std::to_underlying(tmp.type())) });
            rowRow.insert({ "sender", tmp.sender().toStdString() });
            rowRow.insert({ "receiver", tmp.receiver().toStdString() });
            rowRow.insert({ "amount", tmp.amount().toStdString() });
            rowRow.insert({ "date", QByteArray::number(tmp.date()).toStdString() });
            rowRow.insert({ "token", tmp.token().toStdString() });
            rowRow.insert({ "data", tmp.data() });
            rowRow.insert({ "prevBlock", tmp.prevBlock().toStdString() });
            rowRow.insert({ "hash", tmp.hash() });
            rowRow.insert({ "approver", tmp.approver().toStdString() });
            rowRow.insert({ "signature", tmp.signature() });
            rowRow.insert({ "producer", tmp.producer().toStdString() });

            bool txInserted = db.insert(Config::DataStorage::TxBlockTable, rowRow);
            if (txInserted)
                countTransactions++;
        }

        auto signatures = block.signatures();
        for (const auto &[actorId, sign] : std::as_const(signatures)) {
            DBRow rowRow;
            rowRow.insert({ "actorId", actorId.toStdString() });
            rowRow.insert({ "signature", sign });
            rowRow.insert({ "isApprove", "1" /*std::to_string(sign.isApprove)*/ });
            db.insert(Config::DataStorage::SignTable, rowRow);
        }

        if (block.getType() == BlockType::Data)
            realBlockRecords++;

        this->records = records + 1;

        if (id > this->lastSavedId) {
            this->lastSavedId = id;
        }

        if (id < this->firstSavedId || firstSavedId.isEmpty()) {
            qDebug() << "[BlockIndex] First saved id is updated from" << firstSavedId << "to" << id;
            this->firstSavedId = id;
        }

        return BlockVariant(block);
    }
}

bool BlockIndex::hasRecordLimit() const {
    return !this->recordsLimit.isEmpty();
}

bool BlockIndex::recordLimitIsReached() const {
    return this->hasRecordLimit() && (this->records >= this->recordsLimit);
}

int BlockIndex::removeById(const BigNumber &id) {
    auto block = getBlockById(id);

    if (!block.has_value())
        return -1;

    return removeById(block.value());
}

int BlockIndex::removeById(const BlockVariant &block) {
    //    block.getIndex(), block.getType()
    BigNumber id             = block.getIndex();
    BlockType typeBlock      = block.getType();
    auto      countTxInBlock = block.transactions().size();

    if (block.getType() != BlockType::Dummy) {
        qDebug() << "[BlockIndex] Removing block with id" << id << block.getType();
    }

    // if (id < firstSavedId) {
    //     removeAll();
    // }

    BigNumber currentIdToRemove = id;

    QString pathToFile = buildFilePath(currentIdToRemove);
    // qDebug() << "[BlockIndex] To remove:" << pathToFile;
    QFile file(pathToFile);

    if (file.exists() && !file.isOpen()) {
        bool isRemoved = file.remove();
        if (isRemoved) {
            this->records--;
        }

        if (isRemoved && (typeBlock == BlockType::Data || typeBlock == BlockType::Genesis)) {
            this->realBlockRecords--;
            countTransactions -= countTxInBlock;
        }
    }

    // this->lastSavedId = BigNumber(id) - 1;
    return 0;
}

void BlockIndex::removeDummyBlocks() {
    std::vector<std::string> removedForLogs;
    bool                     isNotDummyBlock = false;

    if (lastSavedId < 0 && firstSavedId <= 0)
        return;

    for (auto i = firstSavedId; i <= lastSavedId; i++) {
        const auto block = getBlockById(i);

        if (!block.has_value() || (block.has_value() && block->isEmpty())) {
            continue;
        }

        if (block->getType() == BlockType::Dummy) {
            removeById(block.value());
            removedForLogs.push_back(block->getIndex().toStdString());
        }
    }

    if (!removedForLogs.empty()) {
        qDebug() << "[BlockIndex] Remove dummy blocks:" << removedForLogs;
    }
}

void BlockIndex::removeAll() {
    QString folderPath = this->getFolderPath();
    qDebug() << "Clearing file index:" << folderPath;

    QDir       folder(folderPath);
    const auto folders =
        folder.entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name);
    for (const QString &section : std::as_const(folders)) {
        QDir dir(folderPath + QString("/") + section);
        dir.removeRecursively();
    }

    // update state
    this->records           = 0;
    this->firstSavedId      = 0;
    this->lastSavedId       = 0;
    this->realBlockRecords  = 0;
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

std::expected<BlockVariant, BlockError> BlockIndex::getByIdUnsafe(const BigNumber &id) const {
    std::string path = buildFilePath(id).toStdString();

    if (!std::filesystem::exists(path)) {
        // qDebug() << "[BlockIndex] Can't get the file" << path << "(file is not exits)";
        return std::unexpected(BlockError::NotExists);
    }

    DBConnector db(path, m_blockCompress ? DBConnectorType::Compressed : DBConnectorType::Regular);

    if (!db.open() || db.tableNames().empty()) {
        return std::unexpected(BlockError::NotExists);
    }

    bool              isGenesis = db.tableNames()[0] == "GenesisBlock";
    const std::string blockTable =
        isGenesis ? Config::DataStorage::GenesisBlockTable : Config::DataStorage::BlockTable;

    std::vector<DBRow> res = db.selectAll(blockTable);
    if (res.empty()) {
        return std::unexpected(BlockError::NotExists);
    }

    BigNumber blockId = BigNumber(res[0].at("id"));
    long long date    = std::stoll(res[0].at("date"));

    if (id != blockId) {
        return std::unexpected(BlockError::Invalid);
    }

    std::vector<DBRow> dbSigns = db.select("SELECT * FROM " + Config::DataStorage::SignTable + ";");
    Signatures         signatures;

    for (const auto &dbSign : dbSigns) {
        // .isApprove = boost::lexical_cast<bool>(dbSign.at("isApprove")) };
        signatures[ActorId(dbSign.at("actorId"))] = dbSign.at("signature");
    }

    if (isGenesis) {
        std::string prevGenHash = std::move(res[0].at("prevGenHash"));

        std::vector<DBRow> rows = db.selectAll(Config::DataStorage::RowGenesisBlockTable);
        GenesisDataRows    dataRows;
        for (const auto &row : rows) {
            GenesisDataInfo dRow;
            dRow.type    = DataStorage::DataRowType(QByteArray(row.at("type").c_str()).toInt());
            dRow.state   = BigNumberFloat(row.at("state"));
            auto actorId = row.at("actorId");
            auto tokenId = row.at("token");
            dataRows.insert({ { actorId, tokenId }, dRow });
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

        return BlockVariant(block);
    } else {
        std::vector<DBRow>    rows = db.selectAll(Config::DataStorage::TxBlockTable);
        std::set<Transaction> transactions;

        for (const auto &tmp : rows) {
            Transaction tx;
            tx.setType(TransactionType(std::stoi(tmp.at("type"))));
            tx.setSender(ActorId(tmp.at("sender")));
            tx.setReceiver(ActorId(tmp.at("receiver")));
            tx.setAmount(BigNumberFloat(tmp.at("amount")));
            tx.setDate(std::stoll(tmp.at("date")));
            tx.setData(tmp.at("data"));
            tx.setToken(ActorId(tmp.at("token")));
            tx.setPrevBlock(BigNumber(tmp.at("prevBlock")));
            tx.setHash(tmp.at("hash").c_str());
            tx.setApprover(ActorId(tmp.at("approver")));
            tx.setSignature(tmp.at("signature").c_str());
            tx.setProducer(ActorId(tmp.at("producer")));

            if (!tx.isEmpty() || tx.isBurn()) // TODO: ?
                transactions.insert(tx);
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

        return BlockVariant(block);
    }
}

std::expected<BlockVariant, BlockError> BlockIndex::getById(const BigNumber &id) const {
    try {
        auto res = getByIdUnsafe(id);
        return res;
    } catch (const std::exception &e) {
        return std::unexpected(BlockError::NotExists);
    }
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
    std::function<QString(const QStringList &files)>   getFile) {
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
