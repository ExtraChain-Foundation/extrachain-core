/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include "blockchain/block_index.h"

#include <QDir>
#include <QFileInfoList>

#include "blockchain/blockchain.h"

BlockIndex::BlockIndex() {
    this->sectionSize = Config::DataStorage::SECTION_SIZE;

    QFile file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
    if (file.open(QFile::ReadOnly)) {
        auto last_id_content = file.readAll();

        auto block_range = Json::deserialize<BlockRange>(last_id_content.toStdString());
        if (block_range.has_value()) {
            auto first_id_result = BigNumber::create(block_range->first);
            auto last_id_result  = BigNumber::create(block_range->last);

            if (!first_id_result.has_value() || !last_id_result.has_value()) {
                return;
            }

            last_saved_id  = last_id_result.value();
            first_saved_id = first_id_result.value();

            return;
        }
    } else {
        QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER)).removeRecursively();
        QFile::remove("tmp/cachedTxs.db");
    }

    // last id > 0 -> first id
    // if no zero block or no last block ->
    if (!QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER)).exists()) {
        QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER));
    }

    QFile::remove(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER) + '/' + "last_id");
}

void BlockIndex::setBlockCompress(bool newBlockCompress) {
    m_blockCompress = newBlockCompress;
}

std::expected<BlockVariant, BlockError> BlockIndex::addBlock(const BlockVariant &block) {
    auto result = this->add(block.id(), block);
    return result;
}

std::expected<BlockVariant, BlockError> BlockIndex::getLastBlock() const {
    return this->read_block_by_id(last_saved_id);
}

std::expected<BlockVariant, BlockError> BlockIndex::getLastGenesisBlock(const BigNumber &from) const {
    BigNumber id = Blockchain::calculate_genesis_id_for_block(from >= 0 ? from : this->last_saved_id);

    while (id >= getFirstSavedId()) {
        auto block = this->getGenesisBlockById(id);

        if (block.has_value() && !block->isEmpty()) {
            // eLog("[BlockIndex] {} block found", block->id());
            return block.value();
        }

        id -= Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
    }

    // eFatal("No genesis?");
    return std::unexpected(BlockError::NoGenesis); // BlockError::BlockNotExists?
}

std::expected<BlockVariant, BlockError> BlockIndex::getGenesisBlockById(const BigNumber &id) const {
    auto block = this->getById(id);

    if (block.has_value() && !block->isEmpty() && block->is_genesis()) {
        return block;
    }

    return std::unexpected(BlockError::NoGenesis);
}

std::expected<BlockVariant, BlockError> BlockIndex::read_block_by_id(const BigNumber &id) const {
    if (id < 0) {
        return std::unexpected(BlockError::NotExists);
        // eFatal("getBlockById < 0");
    }

    auto block = this->getById(id);
    if (block.has_value() && !block->isEmpty()) {
        return block;
    }

    // eLog("[BlockIndex] {} not exists, maybe past dummy?", id);
    return std::unexpected(BlockError::NotExists);
}

std::expected<BlockVariant, BlockError> BlockIndex::search_block_by_hash(const std::string &hash) const {
    return getBlockByParam(hash, SearchEnum::BlockParam::Hash);
}

std::expected<BlockVariant, BlockError> BlockIndex::getBlockByData(const std::string &data) const {
    return getBlockByParam(data, SearchEnum::BlockParam::Data);
}

std::expected<BlockVariant, BlockError> BlockIndex::getBlockByParam(const std::string     &id,
                                                                    SearchEnum::BlockParam param) const {
    if (param == SearchEnum::BlockParam::Id) {
        return read_block_by_id(BigNumber(id));
    }

    BigNumber lastBlockId = getLastSavedId();

    // iteration from the last to the first Block
    while (lastBlockId >= getFirstSavedId()) {
        auto lastBlock = read_block_by_id(lastBlockId);

        if (!lastBlock.has_value())
            return std::unexpected(BlockError::NotExists);
        if (lastBlock->isEmpty())
            return std::unexpected(BlockError::NotExists);

        switch (param) {
        case SearchEnum::BlockParam::Data: {
            if (lastBlock->dataService().contains(id))
                return lastBlock;
            break;
        }
        case SearchEnum::BlockParam::Hash: {
            if (lastBlock->getHash() == id)
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

std::pair<Transaction, BigNumber> BlockIndex::search_duplicate(const std::string &hash) const {
    BigNumber records     = getLastSavedId();
    BigNumber lastBlockId = getLastSavedId();

    if (records <= 0) {
        eLog("[BlockIndex] There no tx's in block index");
        return { Transaction(), BigNumber("-1") };
    }

    auto to_block = std::max(getFirstSavedId(), lastBlockId - 30);

    while (lastBlockId >= to_block) {
        auto lastBlock = read_block_by_id(lastBlockId);

        if (!lastBlock.has_value()) {
            --lastBlockId;
            continue;
        }

        if (lastBlock->is_genesis() || lastBlock->isEmpty()) {
            --lastBlockId;
            continue;
        }

        auto txs = lastBlock->transactions();

        for (const Transaction &tx : txs) {
            if (tx.hash() == hash) {
                return { tx, lastBlockId };
            }
        }

        --lastBlockId;
    }

    return { Transaction(), BigNumber("-1") };
}

std::pair<Transaction, BigNumber> BlockIndex::getLastTxByHash(const std::string &hash,
                                                              const TokenId     &token) const {
    return getLastTxByParam(hash, SearchEnum::TxParam::Hash, token);
}

std::pair<Transaction, BigNumber> BlockIndex::getLastTxByData(const std::string &data,
                                                              const TokenId     &token) const {
    return getLastTxByParam(data, SearchEnum::TxParam::Data, token);
}

std::pair<Transaction, BigNumber> BlockIndex::getLastTxBySender(const ActorId &id, const TokenId &token) const {
    return getLastTxByParam(id.to_string(), SearchEnum::TxParam::UserSender, token);
}

std::pair<Transaction, BigNumber> BlockIndex::getLastTxByReceiver(const ActorId &id, const TokenId &token) const {
    return getLastTxByParam(id.to_string(), SearchEnum::TxParam::UserReceiver, token);
}

std::pair<Transaction, BigNumber> BlockIndex::getLastTxBySenderOrReceiver(const ActorId &id,
                                                                          const TokenId &token) const {
    return getLastTxByParam(id.to_string(), SearchEnum::TxParam::UserSenderOrReceiver, token);
}

std::pair<Transaction, BigNumber> BlockIndex::getLastTxBySenderOrReceiverAndToken(const ActorId &id,
                                                                                  const TokenId &token) const {
    return getLastTxByParam(id.to_string(), SearchEnum::TxParam::UserSenderOrReceiverOrToken, token);
}

std::unordered_map<ActorId, std::vector<Transaction>> BlockIndex::getTxsBySenderOrReceiverInRow(
    const std::vector<ActorId> &actor_ids,
    BigNumber                   from,
    int                         count,
    const ActorId              &token) const {
    qDebug() << "getLastTxByParam2";
    return getTxsByParamInRow(actor_ids, SearchEnum::TxParam::UserSenderOrReceiver, from, count, token);
}

std::pair<Transaction, BigNumber> BlockIndex::getLastTxByParam(const std::string  &data,
                                                               SearchEnum::TxParam param,
                                                               const TokenId      &tokenId) const {
    BigNumber records = getLastSavedId();

    if (records <= 0) {
        eLog("[BlockIndex] There no tx's in block index");
        return { Transaction(), BigNumber("-1") };
    }

    BigNumber lastBlockId = getLastSavedId();

    // iterating from last to first block
    while (lastBlockId >= getFirstSavedId()) {
        auto lastBlock = read_block_by_id(lastBlockId);
        if (!lastBlock.has_value()) {
            --lastBlockId;
            continue;
        }
        if (lastBlock->is_genesis() || lastBlock->isEmpty()) {
            --lastBlockId;
            continue;
        }

        auto txs = lastBlock->transactions();

        for (const Transaction &tx : txs) {
            if (tx.token() != tokenId)
                continue;
            switch (param) {
            case SearchEnum::TxParam::UserSenderOrReceiverOrToken: {
                if (tx.sender().to_string() == data || tx.receiver().to_string() == data)
                    return { tx, lastBlockId };
                break;
            }
            case SearchEnum::TxParam::UserSender: {
                if (tx.sender().to_string() == data)
                    return { tx, lastBlockId };
                break;
            }
            case SearchEnum::TxParam::UserReceiver: {
                if (tx.receiver().to_string() == data)
                    return { tx, lastBlockId };
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver: {
                if (tx.sender().to_string() == data || tx.receiver().to_string() == data)
                    return { tx, lastBlockId };
                break;
            }
            case SearchEnum::TxParam::Hash: {
                if (tx.hash() == data)
                    return { tx, lastBlockId };
                break;
            }
            case SearchEnum::TxParam::Data: {
                if (lastBlock->getType() == BlockType::Genesis)
                    return { Transaction(), BigNumber("-1") };
                if (tx.data() == data)
                    return { tx, lastBlockId };
                break;
            }
            default: {
                break;
            }
            }
        }
        --lastBlockId;
    }
    return { Transaction(), BigNumber("-1") };
}

std::unordered_map<ActorId, std::vector<Transaction>> BlockIndex::getTxsByParamInRow(
    const std::vector<ActorId> &actor_ids,
    SearchEnum::TxParam         param,
    BigNumber                   from,
    int                         count,
    ActorId                     token) const {
    std::unordered_map<ActorId, std::vector<Transaction>> currentTxs;

    if (first_saved_id == -1 || last_saved_id == -1) {
        eLog("[BlockIndex] There no tx's in block index");
        return currentTxs;
    }

    BigNumber lastBlockId  = from == -1 ? getLastSavedId() : from;
    int       currentCount = 0;

    while (lastBlockId >= getFirstSavedId()) {
        // qDebug() << "getTxsBySenderOrReceiverInRow3";
        // eLog("{} {}", count, currentCount);

        if (count < currentCount)
            break;

        auto lastBlock = read_block_by_id(lastBlockId);

        if (!lastBlock.has_value() || (lastBlock.has_value() && lastBlock->is_genesis())) {
            --lastBlockId;
            continue;
        }

        auto txs = lastBlock->transactions();

        for (const Transaction &tx : txs) {
            for (const auto &id : actor_ids) {
                if (tx.token() != token)
                    continue;
                switch (param) {
                case SearchEnum::TxParam::UserSender: {
                    if (tx.sender() == id && tx.token() == token) {
                        currentTxs[id].push_back(tx);
                        ++currentCount;
                    }
                    break;
                }
                case SearchEnum::TxParam::UserReceiver: {
                    if (tx.receiver() == id && tx.token() == token) {
                        currentTxs[id].push_back(tx);
                        ++currentCount;
                    }
                    break;
                }
                case SearchEnum::TxParam::UserSenderOrReceiver: {
                    if ((tx.sender() == id || tx.receiver() == id) && tx.token() == token) {
                        currentTxs[id].push_back(tx);
                        ++currentCount;
                    }
                    break;
                }
                case SearchEnum::TxParam::Hash: {
                    //     if (BigNumber(tx.hash()) == id && tx.token() == token) {
                    //         currentTxs.insert(tx);
                    //         ++currentCount;
                    //     }
                    break;
                }
                default: {
                }
                }
            }
        }

        --lastBlockId;
    }

    // eLog("currentTxs {}", currentTxs.length());

    return currentTxs;
}

std::string BlockIndex::buildFilePath(const BigNumber &id) const {
    BigNumber   section        = this->calcSection(id);
    std::string pathToFolder   = getFolderPath() + "/" + section.to_string();
    auto        pathToFolderQt = QString::fromStdString(pathToFolder);

    QDir dir(pathToFolderQt);
    if (!dir.exists()) {
        eLog("[BlockIndex] Creating dir: {}", pathToFolder);
        dir = QDir();
        dir.mkpath(pathToFolderQt);
    }

    return pathToFolder + "/" + id.to_string();
}

void BlockIndex::update_last_id(const BigNumber &id) {
    this->last_saved_id  = id;
    this->first_saved_id = first_saved_id == -1 ? id : std::min(id, this->first_saved_id);

    std::string json =
        Json::serialize(BlockRange { .first = first_saved_id.to_string(), .last = last_saved_id.to_string() });
    QFile file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
    file.open(QFile::WriteOnly);
    file.write(json.data());
    file.close();
}

std::expected<BlockVariant, BlockError> BlockIndex::add(const BigNumber &id, const BlockVariant &newBlock) {
    QString path = QString::fromStdString(buildFilePath(id));

    QFile file(path);
    if (file.exists()) {
        auto block = read_block_by_id(id);

        if (block.has_value() && !block->isEmpty()) {
            // if sign -> add only signs
            if (block == newBlock) {
                return std::unexpected(BlockError::Equal);
            }

            return std::unexpected(BlockError::AlreadyExists);
        }

        file.remove();
    }

    // if (recordLimitIsReached()) {
    // start from genesis, remove first 100 blocks?
    // if (this->firstSavedId != 0) {
    //     this->removeById(getBlockById(getFirstSavedId()));
    //     this->firstSavedId++; // todo: check!
    // }
    // }

    DbConnector db(path.toStdString(), m_blockCompress ? DbConnectorType::Compressed : DbConnectorType::Regular);

    auto bl = newBlock;
    if (!db.open()) {
        return std::unexpected(BlockError::DbNotOpen);
    }

    if (newBlock.is_genesis()) {
        GenesisBlock block = bl.getGenesisBlock()->get();

        db.create_table(Config::DataStorage::GenesisBlockTableCreate);
        db.create_table(Config::DataStorage::RowGenesisBlockTableCreate);
        db.create_table(Config::DataStorage::SignBlockTableCreate);

        DbRow row;
        row.insert({ "type", block.getTypeStr() });
        row.insert({ "id", block.id().to_string() });
        row.insert({ "date", QByteArray::number(block.getDate()).toStdString() });
        row.insert({ "data", block.getDataMessagePack() });
        row.insert({ "prevHash", block.getPrevHash() });
        row.insert({ "hash", block.getHash() });
        row.insert({ "prevGenHash", block.getPrevGenHash() });
        db.insert(Config::DataStorage::GenesisBlockTable, row);

        auto rows = block.dataRows();
        for (const auto &[key, row] : std::as_const(rows)) {
            const auto &[actorId, tokenId] = key;
            DbRow rowRow;
            rowRow.insert({ "actorId", actorId.to_string() });
            rowRow.insert({ "state", row.state.to_string() });
            rowRow.insert({ "token", tokenId.to_string() });
            rowRow.insert({ "type", std::to_string(std::to_underlying(row.type)) });
            db.insert(Config::DataStorage::RowGenesisBlockTable, rowRow);
        }

        auto signatures = block.signatures();
        for (const auto &[actorId, sign] : std::as_const(signatures)) {
            DbRow rowRow;
            rowRow.insert({ "actorId", actorId.to_string() });
            rowRow.insert({ "signature", ByteArray(sign).toBase64() });
            rowRow.insert({ "isApprove", "1" /*std::to_string(sign.isApprove)*/ });
            db.insert(Config::DataStorage::SignTable, rowRow);
        }

        if (id > this->last_saved_id) {
            update_last_id(id);
        }

        if (id < this->first_saved_id || first_saved_id == -1) {
            eLog("[BlockIndex] First saved id is updated from {} to {}", first_saved_id, id);
            this->first_saved_id = id;
        }

        return BlockVariant(block);
    } else {
        Block block = bl.getBlock()->get();

        db.create_table(Config::DataStorage::BlockTableCreate);
        db.create_table(Config::DataStorage::TxBlockTableCreate);
        db.create_table(Config::DataStorage::SignBlockTableCreate);
        DbRow row;

        row.insert({ "type", block.getTypeStr() });
        row.insert({ "id", block.id().to_string() });
        row.insert({ "date", std::to_string(block.getDate()) });
        row.insert({ "data", block.getDataMessagePack() });
        row.insert({ "prevHash", block.getPrevHash() });
        row.insert({ "hash", block.getHash() });
        db.insert(Config::DataStorage::BlockTable, row);

        auto rows = block.transactions();
        for (const auto &tmp : std::as_const(rows)) {
            DbRow rowRow;
            rowRow.insert({ "type", std::to_string(std::to_underlying(tmp.type())) });
            rowRow.insert({ "sender", tmp.sender().to_string() });
            rowRow.insert({ "receiver", tmp.receiver().to_string() });
            rowRow.insert({ "amount", tmp.amount().to_string() });
            rowRow.insert({ "token", tmp.token().to_string() });
            rowRow.insert({ "data", tmp.data() });
            rowRow.insert({ "prev_block", tmp.prevBlock().to_string() });
            rowRow.insert({ "hash", tmp.hash() });
            rowRow.insert({ "signature", ByteArray(tmp.signature()).toBase64() });

            bool txInserted = db.insert(Config::DataStorage::TxBlockTable, rowRow);
            // if (txInserted)
            // countTransactions++;
        }

        auto signatures = block.signatures();
        for (const auto &[actorId, sign] : std::as_const(signatures)) {
            DbRow rowRow;
            rowRow.insert({ "actorId", actorId.to_string() });
            rowRow.insert({ "signature", ByteArray(sign).toBase64() });
            rowRow.insert({ "isApprove", "1" /*std::to_string(sign.isApprove)*/ });
            db.insert(Config::DataStorage::SignTable, rowRow);
        }

        if (id > this->last_saved_id) {
            update_last_id(id);
        }

        if (id < this->first_saved_id || first_saved_id == -1) {
            eLog("[BlockIndex] First saved id is updated from {} to {}", first_saved_id, id);
            this->first_saved_id = id;
        }

        return BlockVariant(block);
    }
}

bool BlockIndex::hasRecordLimit() const {
    return this->recordsLimit != -1;
}

// bool BlockIndex::recordLimitIsReached() const {
//     return this->hasRecordLimit() && (this->records >= this->recordsLimit);
// }

int BlockIndex::removeById(const BigNumber &id) {
    auto block = read_block_by_id(id);

    if (!block.has_value())
        return -1;

    return removeById(block.value());
}

int BlockIndex::removeById(const BlockVariant &block) {
    //    block.id(), block.getType()
    BigNumber id = block.id();
    // BlockType typeBlock      = block.getType();
    // auto      countTxInBlock = block.transactions().size();

    // if (block.getType() != BlockType::Dummy) {
    //     eLog("[BlockIndex] Removing block with id {} {}", id, block.getType());
    // }

    // if (id < firstSavedId) {
    //     removeAll();
    // }

    BigNumber currentIdToRemove = id;

    QString pathToFile = QString::fromStdString(buildFilePath(currentIdToRemove));
    // eLog("[BlockIndex] To remove: {}", pathToFile);
    QFile file(pathToFile);

    if (file.exists() && !file.isOpen()) {
        bool isRemoved = file.remove();
        update_last_id(this->last_saved_id - 1);
        // if (isRemoved) {
        //     this->records--;
        // }

        // if (isRemoved && (typeBlock == BlockType::Data || typeBlock == BlockType::Genesis)) {
        //     countTransactions -= countTxInBlock;
        // }
    } else {
        if (file.exists()) {
            eFatal("Blockchain remove block error");
        }
    }

    return 0;
}

void BlockIndex::removeAll() {
    std::string folderPath = this->getFolderPath();
    eLog("Clearing file index: {}", folderPath);

    auto res = QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER)).removeRecursively();
    eLog("[BlockIndex] RemoveAll: {}", res);
    // auto       folderPathQt = QString::fromStdString(folderPath);
    // QDir       folder(folderPathQt);
    // const auto folders =
    //     folder.entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name);
    // for (const QString &section : std::as_const(folders)) {
    //     QDir dir(folderPathQt + QString("/") + section);
    //     dir.removeRecursively();
    // }

    // update state
    // this->records           = 0;
    this->first_saved_id = -1;
    this->last_saved_id  = -1;
    // QFile::remove(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
    QFile::remove("tmp/cachedTxs.db");
    QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER));
    // this->countTransactions = 0;
}

std::string BlockIndex::getFolderPath() const {
    return BlockchainConst::BLOCKCHAIN_FOLDER;
}

BigNumber BlockIndex::getFirstSavedId() const {
    return this->first_saved_id;
}

BigNumber BlockIndex::calcSection(BigNumber id) const {
    return id / BigNumber(sectionSize);
}

BigNumber BlockIndex::getLastSavedId() const {
    return this->last_saved_id;
}

std::expected<BlockVariant, BlockError> BlockIndex::getByIdUnsafe(const BigNumber &id) const {
    std::string path = buildFilePath(id);

    if (!std::filesystem::exists(path)) {
        // eLog("[BlockIndex] Can't get the file {} (file is not exist)", path);
        return std::unexpected(BlockError::NotExists);
    }

    DbConnector db(path, m_blockCompress ? DbConnectorType::Compressed : DbConnectorType::Regular);

    if (!db.open() || db.table_names().empty()) {
        return std::unexpected(BlockError::NotExists);
    }

    bool isGenesis = db.table_names()[0] == "GenesisBlock";

    const std::string blockTable =
        isGenesis ? Config::DataStorage::GenesisBlockTable : Config::DataStorage::BlockTable;

    std::vector<DbRow> res = db.select_all(blockTable);
    if (res.empty()) {
        return std::unexpected(BlockError::NotExists);
    }

    BigNumber     blockId = BigNumber(res[0].at("id"));
    std::uint64_t date    = std::stoll(res[0].at("date"));

    if (id != blockId) {
        return std::unexpected(BlockError::IdNotEqual);
    }

    std::vector<DbRow> dbSigns = db.select("SELECT * FROM " + Config::DataStorage::SignTable + ";");
    Signatures         signatures;

    for (const auto &dbSign : dbSigns) {
        // .isApprove = boost::lexical_cast<bool>(dbSign.at("isApprove")) };
        signatures[ActorId(dbSign.at("actorId"))] =
            ByteArray::fromBase64(dbSign.at("signature")).toArray<crypto_sign_BYTES>();
    }

    if (isGenesis) {
        std::string prevGenHash = std::move(res[0].at("prevGenHash"));

        std::vector<DbRow> rows = db.select_all(Config::DataStorage::RowGenesisBlockTable);
        GenesisDataRows    dataRows;
        for (const auto &row : rows) {
            GenesisDataInfo dRow;
            dRow.type    = BlockchainConst::DataRowType(QByteArray(row.at("type").c_str()).toInt());
            dRow.state   = BigNumberFloat(row.at("state"));
            auto actorId = row.at("actorId");
            auto tokenId = row.at("token");
            dataRows.insert({ { ActorId(actorId), TokenId(tokenId) }, dRow });
        }

        auto block = GenesisBlock(std::move(res[0].at("type")),
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
        std::vector<DbRow>    rows = db.select_all(Config::DataStorage::TxBlockTable);
        std::set<Transaction> transactions;

        for (const auto &tmp : rows) {
            Transaction tx;
            tx.setType(TransactionType(std::stoi(tmp.at("type"))));
            tx.setSender(ActorId(tmp.at("sender")));
            tx.setReceiver(ActorId(tmp.at("receiver")));
            tx.setAmount(BigNumberFloat(tmp.at("amount")));
            tx.setData(tmp.at("data"));
            tx.setToken(ActorId(tmp.at("token")));
            tx.setPrevBlock(BigNumber(tmp.at("prev_block")));
            tx.setHash(tmp.at("hash").c_str());
            tx.setSignature(ByteArray::fromBase64(tmp.at("signature")).toArray<crypto_sign_BYTES>());

            if (!tx.isEmpty() || tx.isBurn()) // TODO: ?
                transactions.insert(tx);
        }

        auto block = Block(std::move(res[0].at("type")),
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
