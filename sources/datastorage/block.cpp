#include "datastorage/block.h"

void Block::setType(const QByteArray &value)
{
    type = value;
}

Block::Block()
{
    this->type = Config::DATA_BLOCK_TYPE;

    this->index = BigNumber(-1);
    this->approver = BigNumber(-1);
    this->date = QDateTime::currentDateTime().toTime_t();
    this->data = "";
    this->prevHash = "";
    this->hash = "";
    this->digSig = "";
}

Block::Block(const Block &block)
{
    this->type = block.getType();
    this->index = block.getIndex();
    this->approver = block.getApprover();
    this->date = block.getDate();
    this->data = block.getData();
    this->prevHash = block.getPrevHash();
    this->hash = block.getHash();
    this->digSig = block.getDigSig();
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
    approver = block.approver;
    date = block.date;
    prevHash = block.prevHash;
    hash = block.hash;
    digSig = block.digSig;
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

QByteArray Block::getDataForHash() const
{
    return type + data + index.toByteArray() + approver.toActorId() + QByteArray::number(date) + prevHash;
}

QByteArray Block::getDataForDigSig() const
{
    return type + data + index.toByteArray() + approver.toActorId() + QByteArray::number(date) + prevHash
        + hash;
}

void Block::sign(const Actor<KeyPrivate> &actor)
{
    this->approver = actor.getId();
    calcHash();
    this->digSig = actor.getKey()->sign(getDataForDigSig());
}

bool Block::verify(const Actor<KeyPublic> &actor) const
{
    bool res = actor.getKey()->verify(getDataForDigSig(), getDigSig());
    //    return res;
    return digSig.isEmpty() ? false : res;
}

bool Block::deserialize(const QByteArray &serialized)
{

    //    QList<QByteArray> list =
    //        Serialization::deserialize(serialized, Serialization::BLOCK_FIELD_SPLITTER);
    QList<QByteArray> list = Serialization::universalDeserialize(serialized, FIELDS_SIZE);

    if (list.length() == 8)
    {

        type = list.at(0);
        index = BigNumber(list.at(1));
        approver = BigNumber(list.at(2));
        date = list.at(3).toLongLong();
        data = list.at(4);
        prevHash = list.at(5);
        hash = list.at(6);
        digSig = list.at(7);
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
    this->data = this->data + data /* + Serialization::DEFAULT_LIST_SPLITTER*/;
}

QList<Transaction> Block::extractTransactions() const
{
    if (type != Config::DATA_BLOCK_TYPE)
    {
        return QList<Transaction>();
    }

    //    QList<QByteArray> txsData =
    //        Serialization::deserialize(data, Serialization::DEFAULT_LIST_SPLITTER);
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

    list << getType() << getIndex().toByteArray() << getApprover().toActorId() << QByteArray::number(date)
         << getData() << getPrevHash() << getHash() << getDigSig();
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
    return this->digSig;
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
    return this->approver;
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
    approver = BigNumber(list.takeFirst());
    data = list.takeFirst();
    date = list.takeFirst().toLongLong();
    prevHash = list.takeFirst();
    hash = list.takeFirst();
    digSig = list.takeFirst();
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

void Block::setApprover(const BigNumber &value)
{
    approver = value;
}
