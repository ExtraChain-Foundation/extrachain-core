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

GenesisBlock::GenesisBlock(const QByteArray &data, const Block &prevBlock, const QByteArray &prevGenHash)
    : Block(data, prevBlock)
    , prevGenHash(prevGenHash)
{
    this->type = Config::GENESIS_BLOCK_TYPE;
}

void GenesisBlock::addRow(const GenesisDataRow &row)
{
    this->data += row.serialize();
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
    QList<QByteArray> l = Serialization::universalDeserialize(serialized, FIELDS_SIZE);
    if (l.length() == 9)
    {
        initFields(l);
        return true;
    }
    return false;
}

QByteArray GenesisBlock::serialize() const
{
    QList<QByteArray> list;
    list << getType() << getIndex().toByteArray() << getApprover().toActorId()
         << QByteArray::number(getDate()) << getData() << getPrevHash() << getHash() << getDigSig()
         << getPrevGenHash();
    return Serialization::universalSerialize(list, FIELDS_SIZE);
}

void GenesisBlock::initFields(QList<QByteArray> &list)
{
    type = list.takeFirst();
    index = BigNumber(list.takeFirst());
    approver = BigNumber(list.takeFirst());
    date = list.takeFirst().toLongLong();
    data = list.takeFirst();
    prevHash = list.takeFirst();
    hash = list.takeFirst();
    digSig = list.takeFirst();
    prevGenHash = list.takeFirst();
}

QList<GenesisDataRow> GenesisBlock::extractDataRows() const
{
    QList<QByteArray> txsData = Serialization::universalDeserialize(data, Serialization::DEFAULT_FIELD_SIZE);
    QList<GenesisDataRow> genesisDataRows;
    for (const QByteArray &dataRow : txsData)
    {
        genesisDataRows.append(GenesisDataRow(dataRow));
    }
    return genesisDataRows;
}

bool GenesisBlock::isGenesisBlock(const QByteArray &serialized)
{
    return serialized.contains(Config::GENESIS_BLOCK_TYPE);
}

void GenesisBlock::setPrevGenHash(const QByteArray &value)
{
    prevGenHash = value;
}

QByteArray GenesisBlock::getPrevGenHash() const
{
    return prevGenHash;
}
