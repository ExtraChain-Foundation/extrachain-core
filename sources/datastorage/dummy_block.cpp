#include "datastorage/dummy_block.h"

DummyBlock::DummyBlock()
    : Block() {
    this->m_type = Config::DUMMY_BLOCK_TYPE;
}

DummyBlock::DummyBlock(const DummyBlock &block)
    : Block(block) {
    this->prevGenHash = block.getPrevGenHash();
    this->m_type = Config::DUMMY_BLOCK_TYPE;
}

DummyBlock::DummyBlock(const QByteArray &serialized) {
    deserialize(serialized);
}

DummyBlock::DummyBlock(const Block &prevBlock)
    : Block(QByteArray(), prevBlock)
    , prevGenHash(prevBlock.getPrevHash()) {
    this->m_type = Config::DUMMY_BLOCK_TYPE;
}

void DummyBlock::addRow(const DummyDataRow &row) {
    this->data += Serialization::serialize({ row.serialize() }, Serialization::DEFAULT_FIELD_SIZE);
}

const std::string &DummyBlock::getDataForDigSig() const {
    return Block::getDataForDigSig();
}

QByteArray DummyBlock::getDataForHash() const {
    return Block::getDataForHash();
}

bool DummyBlock::deserialize(const QByteArray &serialized) {
    *this = MessagePack::deserialize<DummyBlock>(serialized);
    return true;
}

QByteArray DummyBlock::serialize() const {
    return QByteArray::fromStdString(MessagePack::serialize(*this));
}

void DummyBlock::initFields(QList<QByteArray> &list) {
    m_type = list.takeFirst();
    index = BigNumber(list.takeFirst().toStdString());
    date = list.takeFirst().toLongLong();
    data = list.takeFirst();
    prevHash = list.takeFirst();
    hash = list.takeFirst();
    prevGenHash = list.takeFirst();
    QByteArray signs = list.takeFirst();
    QByteArrayList lists = Serialization::deserialize(signs, FIELDS_SIZE);
    for (const auto &tmp : lists) {
        QByteArrayList tmps = Serialization::deserialize(tmp, FIELDS_SIZE);
        if (tmps.length() == 3)
            signatures.push_back(
                { tmps.at(0).toStdString(), tmps.at(1).toStdString(), bool(tmps.at(2).toInt()) });
    }
}

QList<DummyDataRow> DummyBlock::extractDataRows() const {
    QList<QByteArray> txsData =
        Serialization::deserialize(QByteArray::fromStdString(data), Serialization::DEFAULT_FIELD_SIZE);
    QList<DummyDataRow> dummyDataRows;
    for (const QByteArray &dataRow : txsData) {
        dummyDataRows.append(DummyDataRow(dataRow));
    }
    return dummyDataRows;
}

bool DummyBlock::isDummyBlock(const QByteArray &serialized) {
    return serialized.contains(Config::DUMMY_BLOCK_TYPE);
}

void DummyBlock::setPrevGenHash(const std::string &value) {
    prevGenHash = value;
}

std::string DummyBlock::getPrevGenHash() const {
    return prevGenHash;
}
