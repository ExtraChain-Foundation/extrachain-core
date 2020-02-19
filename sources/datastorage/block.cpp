#include "datastorage/block.h"

Block::Block()
{
    this->type = Config::DATA_BLOCK_TYPE;

    this->index = BigNumber(-1);
    this->date = QDateTime::currentDateTime().toTime_t();
    this->data = "";
    this->prevHash = "";
    this->hash = "";
}

Block::Block(const Block &block)
{
    this->type = block.getType();
    this->index = block.getIndex();
    this->date = block.getDate();
    this->data = block.getData();
    this->prevHash = block.getPrevHash();
    this->hash = block.getHash();
    this->signatures = block.signatures;
}

Block::Block(const QByteArray &serialized)
    : Block()
{
    this->deserialize(serialized);
}

Block::Block(const QByteArray &data, const Block &prev)
    : Block()
{
    if (prev.isEmpty())
    {
        // qDebug() << "BLOCK: Construction first block";
        this->index = BigNumber("0");
        this->prevHash = Utils::calcKeccak(QByteArray("0 index"));
    }
    else
    {
        // qDebug() << "BLOCK: Construction block. Previous block id - "
        //          << prev->getIndex();
        this->index = prev.getIndex() + 1;
        this->prevHash = prev.getHash();
    }

    this->date = QDateTime::currentDateTime().toTime_t();

    this->data = data;
}

Block::~Block()
{
}

Block Block::operator=(const Block &block)
{
    type = block.type;
    data = block.data;
    index = block.index;
    date = block.date;
    prevHash = block.prevHash;
    hash = block.hash;
    signatures = block.signatures;
    return *this;
}

void Block::calcHash()
{
    QByteArray resultHash = Utils::calcKeccak(getDataForHash());
    if (!resultHash.isEmpty())
    {
        this->hash = resultHash;
    }
}

void Block::setType(const QByteArray &value)
{
    type = value;
}

QByteArray Block::getDataForHash() const
{
    QByteArray idHash = Utils::calcKeccak(getIndex().toByteArray());
    QList<Transaction> list = extractTransactions();
    if (list.isEmpty())
        return idHash;
    QByteArray txHash = Utils::calcKeccak(list[0].serialize());
    for (int i = 1; i < list.size(); i++)
    {
        QByteArray tmpTxHash = Utils::calcKeccak(list[i].serialize());
        txHash = Utils::calcKeccak(txHash + tmpTxHash);
    }
    return idHash + txHash;
}

QByteArray Block::getDataForDigSig() const
{
    return hash;
}

void Block::sign(const Actor<KeyPrivate> &actor)
{
    calcHash();
    QByteArray sign = actor.getKey()->sign(getDataForDigSig());
    this->signatures.insert(actor.getId().toByteArray(), sign);
}

bool Block::verify(const Actor<KeyPublic> &actor) const
{
    bool res = actor.getKey()->verify(getDataForDigSig(), getDigSig());
    return signatures.isEmpty() ? false : res;
}

bool Block::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = Serialization::universalDeserialize(serialized, FIELDS_SIZE);

    if (list.length() == 7)
    {

        type = list.at(0);
        index = BigNumber(list.at(1));
        date = list.at(2).toLongLong();
        data = list.at(3);
        prevHash = list.at(4);
        hash = list.at(5);
        QByteArray signs = list.at(6);
        QByteArrayList lists = Serialization::universalDeserialize(signs, FIELDS_SIZE);
        for (const auto &tmp : lists)
        {
            QByteArrayList tmps = Serialization::universalDeserialize(tmp, FIELDS_SIZE);
            signatures.insert(tmps.at(0), tmps.at(1));
        }
        if (isEmpty())
        {
            qDebug() << "Can't deserialize, block" << getIndex() << "is empty";
            return false;
        }
        return true;
    }
    return false;
}

bool Block::equals(const Block &block) const
{
    return hash == block.getHash();
}

BlockCompare Block::compareBlock(const Block &b) const
{
    BlockCompare temp;
    temp.approverDiff = getApprover() - b.getApprover();
    temp.indexDiff = getIndex() - b.getIndex();
    temp.dataDiff = Utils::compare(getData(), b.getData());
    temp.digitalSigDiff = getDigSig() == b.getDigSig();
    temp.hashDiff = getHash() == b.getHash();
    temp.prevHashDiff = getPrevHash() == b.getPrevHash();
    return temp;
}

void Block::addData(const QByteArray &data)
{
    this->data = this->data + Serialization::universalSerialize({ data }, FIELDS_SIZE);
}

QList<Transaction> Block::extractTransactions() const
{
    if (type != Config::DATA_BLOCK_TYPE)
    {
        return QList<Transaction>();
    }

    QList<QByteArray> txsData = Serialization::universalDeserialize(data, FIELDS_SIZE);
    QList<Transaction> transactions;
    for (const QByteArray &trData : txsData)
    {
        Transaction tx(trData);
        if (!tx.isEmpty())
        {
            transactions.append(tx);
        }
    }
    return transactions;
}

bool Block::contain(Block &from) const
{
    QList<Transaction> ourTx = this->extractTransactions();
    QList<Transaction> fromTx = from.extractTransactions();
    for (auto i : fromTx)
    {
        if (!ourTx.contains(i))
            return false;
    }
    return true;
}

QByteArray Block::serialize() const
{
    QList<QByteArray> list;

    list << getType() << getIndex().toByteArray() << QByteArray::number(date) << getData() << getPrevHash()
         << getHash() << getSignatures();
    //    return Serialization::serialize(list, Serialization::BLOCK_FIELD_SPLITTER);
    return Serialization::universalSerialize(list, FIELDS_SIZE);
}

QString Block::toString() const
{
    QList<QByteArray> list;

    list << getType() << getIndex().toByteArray() << getApprover().toActorId() << QByteArray::number(date)
         << getData() << getPrevHash() << getHash() << getDigSig();
    //    return Serialization::serialize(list, Serialization::BLOCK_FIELD_SPLITTER);
    return Serialization::universalSerialize(list, FIELDS_SIZE);
}

bool Block::isEmpty() const
{
    return (this->getHash().isEmpty()) && (this->getDigSig().isEmpty()) && (this->getPrevHash().isEmpty());
}

QByteArray Block::getType() const
{
    return type;
}

QByteArray Block::getDigSig() const
{

    return signatures.isEmpty() ? "" : this->signatures.begin().value();
}

QByteArray Block::getSignatures() const
{
    QByteArray res = "";

    for (auto it = signatures.begin(); it != signatures.end(); it++)
    {
        QByteArray data = Serialization::universalSerialize({ it.key(), it.value() }, FIELDS_SIZE);
        res += Serialization::universalSerialize({ data }, FIELDS_SIZE);
    }
    return res;
}

QByteArrayList Block::getListSignatures() const
{
    QByteArrayList res;

    for (auto it = signatures.begin(); it != signatures.end(); it++)
        res << it.key() << it.value();

    return res;
}

void Block::addSignature(const QByteArray &id, const QByteArray &sign)
{
    this->signatures.insert(id, sign);
}
// void Block::setType(QByteArray type)
//{
//    this->type = type;
//}

void Block::setPrevHash(const QByteArray &value)
{
    prevHash = value;
}

BigNumber Block::getApprover() const
{
    return signatures.isEmpty() ? "" : this->signatures.begin().key();
}

BigNumber Block::getIndex() const
{
    return index;
}

QByteArray Block::getData() const
{
    return data;
}

QByteArray Block::getHash() const
{
    return hash;
}

QByteArray Block::getPrevHash() const
{
    return prevHash;
}

bool Block::operator<(const Block &other)
{
    if (this->index < other.getIndex())
    {
        return true;
    }
    else if (this->data < other.getData())
    {
        return true;
    }
    return false;
}

bool Block::isBlock(const QByteArray &data)
{
    return data.contains(Config::DATA_BLOCK_TYPE);
}

void Block::initFields(QList<QByteArray> &list)
{
    type = list.takeFirst();
    index = BigNumber(list.takeFirst());
    date = list.takeFirst().toLongLong();
    data = list.takeFirst();
    prevHash = list.takeFirst();
    hash = list.takeFirst();
    QByteArray signs = list.takeFirst();
    QByteArrayList lists = Serialization::universalDeserialize(signs, FIELDS_SIZE);
    for (const auto &tmp : lists)
    {
        QByteArrayList tmps = Serialization::universalDeserialize(tmp, FIELDS_SIZE);
        signatures.insert(tmps.at(0), tmps.at(1));
    }
}

QList<Block> Block::getDataFromAllBlocks(QList<QByteArray> paths)
{
    // need to realize -- read only to genesis block
    QList<Block> res;

    //  QString temp;
    for (int count = 0; count++; count < paths.size())
    {

        QFile file(paths.at(count));
        //        deserialize(file.readAll());

        Block temp(file.readAll());
        //    QList<QByteArray> list = Serialization::deserialize(
        //                file.readAll(), Serialization::BLOCK_FIELD_SPLITTER);
        //    if (list.length() == 7)
        //    {

        //        temp.type = list.at(0);
        //        temp.index = BigNumber(list.at(1));
        //        temp.approver = BigNumber(list.at(2));
        //        temp.data = list.at(3);
        //        temp.prevHash = list.at(4);
        //        temp.hash = list.at(5);
        //        temp.digSig = list.at(6);
        //        //return true;

        //    }/
        res.append(temp);
    }

    return res;
}
long long Block::getDate() const
{
    return date;
}

void Block::setDate(long long value)
{
    date = value;
}
