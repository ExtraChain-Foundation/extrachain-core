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

#ifndef BLOCKINDEX_H
#define BLOCKINDEX_H

#include "datastorage/block.h"
#include "datastorage/genesis_block.h"
#include "utils/db_connector.h"

class BlockIndex
{
public:
    BlockIndex();
    BlockIndex(const BigNumber &recordsLimit);

    /// custom folder name
    BlockIndex(const QString &folderName);
    BlockIndex(const QString &folderName, const BigNumber &recordsLimit);

    QString folderName;          // set in subclasses
    int sectionSize;             // todo: 0 = use only one folder
    BigNumber recordsLimit = -1; // -1 = no limit

    // current state //
    BigNumber records = 0;
    BigNumber firstSavedId = -1;
    BigNumber lastSavedId = -1;

public:
    /**
     * Serializes a block and make a file in fs.
     * @param block
     * @return resultCode, 0 - block is saved
     */
    int addBlock(const Block &block);

    /**
     * @brief Get last block (only Block, not Genesis block)
     * @return last block
     */
    Block getLastBlock() const;

    /**
     * @brief Get last genesis block
     * @return last genesis block
     */
    GenesisBlock getLastGenesisBlock() const;
    GenesisBlock getGenesisBlockById(const BigNumber &id) const;

    /**
     * @brief Gets block by in in file index (only Block, not Genesis block)
     * @param id
     * @return block, if is found, otherwise - empty block
     */
    Block getBlockById(const BigNumber &id) const;

    QByteArray getBlockDataById(const BigNumber &id) const;

    // todo: if genesis block is found -> return empty block, or skip in search logic
    Block getBlockByPosition(const BigNumber &position) const;
    Block getBlockByApprover(const BigNumber &approver) const;
    Block getBlockByHash(const QByteArray &hash) const;
    Block getBlockByData(const QByteArray &data) const;

    Block getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const;

    std::pair<Transaction, QByteArray> getLastTxByHash(const QByteArray &hash, const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxBySender(const BigNumber &id, const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxByReceiver(const BigNumber &id,
                                                           const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxBySenderOrReceiver(const BigNumber &id,
                                                                   const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxBySenderOrReceiverAndToken(const BigNumber &id,
                                                                           const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxByApprover(const BigNumber &id,
                                                           const QByteArray &token) const;
    QList<Transaction> getRecentTxList(const BigNumber &last, const BigNumber &first) const;

    QList<Transaction> getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from = -1, int count = 10,
                                                     BigNumber token = 0) const;

    void removeAll();
    BigNumber getLastSavedId() const;
    BigNumber getFirstSavedId() const;
    BigNumber getRecords() const;
    int removeById(const BigNumber &id);
    QString buildFilePath(const BigNumber &id) const;

private:
    std::pair<Transaction, QByteArray> getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param,
                                                        const QByteArray &token) const;
    QList<Transaction> getTxsByParamInRow(const BigNumber &id, SearchEnum::TxParam param, BigNumber from = -1,
                                          int count = 10, BigNumber token = 0) const;

    int add(const BigNumber &id, const QByteArray &_data);
    bool hasRecordLimit() const;
    bool recordLimitIsReached() const;
    QString getFolderPath() const;
    QString getFolderName() const;
    BigNumber calcSection(BigNumber id) const;
    QByteArray getById(const BigNumber &id) const;
    BigNumber loadFirstId();
    BigNumber loadFileFromSection(std::function<QString(const QStringList &folders)> getFolder,
                                  std::function<QString(const QStringList &files)> getFile);

    BigNumber loadLastId();
};

#endif // BLOCKINDEX_H
