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

#ifndef GENESIS_BLOCK_H
#define GENESIS_BLOCK_H

#include "datastorage/block.h"

/**
 * @brief Representation of one row in genesis block data field
 */

class GenesisDataRow
{
public:
    BigNumber actorId;
    BigNumber state;
    BigNumber token;
    DataStorage::typeDataRow type;

public:
    GenesisDataRow()
    {
    }
    GenesisDataRow(const BigNumber &actorId, const BigNumber &state, const BigNumber &token,
                   const DataStorage::typeDataRow &type)
        : actorId(actorId)
        , state(state)
        , token(token)
        , type(type)
    {
    }
    GenesisDataRow(const QByteArray &serialized)
    {
        deserialize(serialized);
    }

    QByteArray serialize() const
    {
        QList<QByteArray> l;
        l << actorId.toActorId() << state.toByteArray() << token.toActorId() << QByteArray::number(type);
        return Serialization::serialize(l, Serialization::DEFAULT_FIELD_SIZE);
    }
    void deserialize(const QByteArray &serialized)
    {
        QList<QByteArray> l =
            Serialization::deserialize(serialized, Serialization::DEFAULT_FIELD_SIZE);
        if (l.size() == 4)
        {
            actorId = BigNumber(l.at(0));
            state = BigNumber(l.at(1));
            token = BigNumber(l.at(2));
            type = DataStorage::typeDataRow(l.at(3).toInt());
        }
    }

    bool operator==(const GenesisDataRow &other)
    {
        return this->actorId == other.actorId && this->state == other.state && this->token == other.token
            && this->type == other.type;
    }
};

namespace Config {
static const QByteArray GENESIS_BLOCK_TYPE = "genesis";
static const QByteArray GENESIS_BLOCK_MERGE = "genesisMerge";
}

/**
 * @brief Genesis block it's an extended block, with has specific data field
 * and one additional field - prevGenHash.
 */
class GenesisBlock : public Block
{
public:
    QByteArray prevGenHash; // previous genesis block hashes

public:
    GenesisBlock();
    GenesisBlock(const GenesisBlock &block);

    // Deserialize already constructed block
    GenesisBlock(const QByteArray &serialized);

    // Initial block construction, prev = nullptr for first block
    GenesisBlock(const QByteArray &_data, const Block &prevBlock, const QByteArray &prevGenHash);

    // Block interface
public:
    void addRow(const GenesisDataRow &row);
    QByteArray getDataForHash() const override;   // deprecate?
    QByteArray getDataForDigSig() const override; // deprecate?
    bool deserialize(const QByteArray &serialized) override;
    QByteArray serialize() const override;
    void initFields(QList<QByteArray> &list) override;

    /**
     * @brief extract non-empty genesisDataRows from data
     * @return genesis data row list
     */
    QList<GenesisDataRow> extractDataRows() const;
    static bool isGenesisBlock(const QByteArray &serialized);

public:
    QByteArray getPrevGenHash() const;
    void setPrevGenHash(const QByteArray &value);
};

#endif // GENESIS_BLOCK_H
