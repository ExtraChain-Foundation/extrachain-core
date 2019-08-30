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
    Transaction tx;

public:
    GenesisDataRow()
    {
    }
    GenesisDataRow(const BigNumber &actorId, const Transaction &tx)
        : actorId(actorId)
        , tx(tx)
    {
    }
    GenesisDataRow(const QByteArray &serialized)
    {
        deserialize(serialized);
    }

    QByteArray serialize() const
    {
        QList<QByteArray> l;
        l << actorId.serialize() << tx.serialize();
        return Serialization::serialize(l, Serialization::GENESIS_ROW_FIELD_SPLITTER);
    }
    void deserialize(const QByteArray &serialized)
    {
        QList<QByteArray> l =
            Serialization::deserialize(serialized, Serialization::GENESIS_ROW_FIELD_SPLITTER);
        if (l.size() == 2)
        {
            actorId = BigNumber(l.at(0));
            tx = l.at(1);
        }
    }

    bool operator==(const GenesisDataRow &other)
    {
        return this->actorId == other.actorId && this->tx == other.tx;
    }
};

static QDataStream &operator<<(QDataStream &in, GenesisDataRow &row)
{
    in << row.actorId.getHexValue() << row.tx.serialize();
    return in;
}
static QDataStream &operator>>(QDataStream &out, GenesisDataRow &row)
{
    QByteArray txSerialized;
    QByteArray actorIdSerialized;

    out >> actorIdSerialized >> txSerialized;

    row.actorId = BigNumber(actorIdSerialized);
    row.tx = Transaction(txSerialized);

    return out;
}

namespace Config {
static const QByteArray GENESIS_BLOCK_TYPE = "genesis";
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
    GenesisBlock(const QByteArray &data, const Block *prevBlock,
                 const QByteArray &prevGenHash);

    // Block interface
public:
    void addRow(const GenesisDataRow &row);
    QByteArray getDataForHash() const override;
    QByteArray getDataForDigSig() const override;
    bool deserialize(const QByteArray &serialized) override;
    QByteArray serialize() const override;

    /**
     * @brief extract non-empty genesisDataRows from data
     * @return genesis data row list
     */
    QList<GenesisDataRow> extractDataRows() const;
    static bool isGenesisBlock(const QByteArray &serialized);

public:
    QByteArray getPrevGenHash() const;
};

#endif // GENESIS_BLOCK_H
