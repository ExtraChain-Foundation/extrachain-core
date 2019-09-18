#include "datastorage/genesis_block.h"

GenesisBlock::GenesisBlock()
    : Block()
{
    this->type = Config::GENESIS_BLOCK_TYPE;
}

GenesisBlock::GenesisBlock(const GenesisBlock &block)
    : Block(block)
{
    this->prevGenHash = block.getPrevGenHash();
}

GenesisBlock::GenesisBlock(const QByteArray &serialized)
{
    deserialize(serialized);
}

GenesisBlock::GenesisBlock(const QByteArray &data, const Block *prevBlock, const QByteArray &prevGenHash)
    : Block(data, prevBlock)
    , prevGenHash(prevGenHash)
{
    this->type = Config::GENESIS_BLOCK_TYPE;
}

void GenesisBlock::addRow(const GenesisDataRow &row)
{
    this->data += (row.serialize() + Serialization::DEFAULT_LIST_SPLITTER);
}

QByteArray GenesisBlock::getDataForDigSig() const
{
    return Block::getDataForDigSig() + prevGenHash;
}

QByteArray GenesisBlock::getDataForHash() const
{
    return Block::getDataForHash() + prevGenHash;
}

bool GenesisBlock::deserialize(const QByteArray &serialized)
{
    //    QList<QByteArray> l = Serialization::deserialize(
    //                serialized, Serialization::BLOCK_FIELD_SPLITTER);
    QList<QByteArray> l = Serialization::universalDesirialize(serialized, FIELDS_SIZE);
    qDebug() << "GenesisBlock::deserialize" << l.length();
    if (l.length() == 8)
    {
        prevGenHash = l.at(7);
        l.removeLast(); // !!!
        return Block::deserialize(Serialization::universalSerialize(l, FIELDS_SIZE));
    }
    return false;
}

QByteArray GenesisBlock::serialize() const
{
    QList<QByteArray> list;
    list << getType() << getIndex().toString().toLocal8Bit() << getApprover().toString().toLocal8Bit()
         << getData() << getPrevHash() << getHash() << getDigSig() << getPrevGenHash();
    return Serialization::universalSerialize(list, FIELDS_SIZE);
}

QList<GenesisDataRow> GenesisBlock::extractDataRows() const
{
    QList<QByteArray> txsData = Serialization::deserialize(data, Serialization::DEFAULT_LIST_SPLITTER);
    QList<GenesisDataRow> genesisDataRows;
    for (const QByteArray &dataRow : txsData)
    {
        genesisDataRows.append(GenesisDataRow(dataRow));
    }
    return genesisDataRows;
}

bool GenesisBlock::isGenesisBlock(const QByteArray &serialized)
{
    QByteArray type(serialized, Config::GENESIS_BLOCK_TYPE.size());
    return type.contains(Config::GENESIS_BLOCK_TYPE);
}

QByteArray GenesisBlock::getPrevGenHash() const
{
    return prevGenHash;
}
