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
        return Serialization::universalSerialize(l, Serialization::DEFAULT_FIELD_SIZE);
    }
    void deserialize(const QByteArray &serialized)
    {
        QList<QByteArray> l =
            Serialization::universalDeserialize(serialized, Serialization::DEFAULT_FIELD_SIZE);
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
    GenesisBlock(const QByteArray &data, const Block &prevBlock, const QByteArray &prevGenHash);

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
