#include "datastorage/transaction.h"

#include <QDateTime>

Transaction::Transaction(QObject *parent)
{
    this->sender = BigNumber(0);
    this->receiver = BigNumber(0);
    this->amount = BigNumber(0);
    this->date = QDateTime::currentDateTime().toTime_t();
    this->data = QByteArray();
    this->token = BigNumber(0);
    this->senderBalance = BigNumber(0);
    this->receiverBalance = BigNumber(0);
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->approver = BigNumber(0);
    this->digSig = QByteArray();

    calcHash();
}

Transaction::Transaction(const QByteArray &serialized, QObject *parent)
    : Transaction()
{
    //    QList<QByteArray> list =
    //        Serialization::deserialize(serialized, Serialization::TX_FIELD_SPLITTER);
    QList<QByteArray> list = Serialization::universalDesirialize(serialized, FIELS_SIZE);
    if (list.size() == 14)
    {
        this->sender = BigNumber(list.at(0));
        this->receiver = BigNumber(list.at(1));
        this->amount = BigNumber(list.at(2));
        this->date = list.at(3).toLongLong();
        this->data = list.at(4);
        this->token = BigNumber::fromByteArray(list.at(5));
        this->senderBalance = BigNumber(list.at(6));
        this->receiverBalance = BigNumber(list.at(7));
        this->prevBlock = BigNumber(list.at(8));
        this->gas = list.at(9).toInt();
        this->hop = list.at(10).toInt();
        this->hash = QByteArray(list.at(11));
        this->approver = BigNumber(list.at(12));
        this->digSig = list.at(13);
    }

    calcHash();
}

Transaction::Transaction(const BigNumber &sender, const BigNumber &receiver, const BigNumber &amount,
                         QObject *parent)
    : Transaction(parent)
{
    this->sender = sender;
    this->receiver = receiver;
    this->amount = amount;
    this->date = QDateTime::currentDateTime().toTime_t();
    this->data = QByteArray();
    this->token = BigNumber(0);
    this->senderBalance = BigNumber(0);
    this->receiverBalance = BigNumber(0);
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->approver = BigNumber(0);
    this->digSig = QByteArray();

    calcHash();
}

Transaction::Transaction(const BigNumber &sender, const BigNumber &receiver, const BigNumber &amount,
                         const QByteArray &data, QObject *parent)
    : Transaction(sender, receiver, amount, parent)
{
    this->data = data;

    calcHash();
}

Transaction::Transaction(const Transaction &other, QObject *parent)
{
    this->sender = other.sender;
    this->receiver = other.receiver;
    this->amount = other.amount;
    this->date = other.date;
    this->data = other.data;
    this->token = other.token;
    this->senderBalance = other.senderBalance;
    this->receiverBalance = other.receiverBalance;
    this->prevBlock = other.prevBlock;
    this->gas = other.gas;
    this->hop = other.hop;
    this->hash = other.hash;
    this->approver = other.approver;
    this->digSig = other.digSig;

    calcHash();
}

void Transaction::setData(const QByteArray &value)
{
    data = value;
}

void Transaction::setToken(const BigNumber &value)
{
    token = value;
}

long long Transaction::getDate() const
{
    return date;
}

void Transaction::setDate(long long value)
{
    date = value;
}

void Transaction::calcHash()
{
    QByteArray resultHash = Utils::calcKeccak(getDataForHash());
    if (!resultHash.isEmpty())
    {
        this->hash = resultHash;
    }
}

QByteArray Transaction::getDataForHash() const
{
    return (sender.serialize() + receiver.serialize() + amount.serialize() + QByteArray::number(date) + data
            + token.toByteArray() + senderBalance.serialize() + receiverBalance.serialize()
            + prevBlock.serialize() + QByteArray::number(gas) + approver.serialize());
}

QByteArray Transaction::getDataForDigSig() const
{
    return getDataForHash() + hash;
}

void Transaction::sign(const Actor<KeyPrivate> &actor)
{
    this->approver = actor.getId();
    calcHash();
    this->digSig = actor.getKey()->sign(getDataForDigSig());
}

bool Transaction::verify(const Actor<KeyPublic> &actor) const
{
    return digSig.isEmpty() ? false : actor.getKey()->verify(getDataForDigSig(), getDigSig());
}

int Transaction::getHop() const
{
    return hop;
}

void Transaction::setSenderBalance(BigNumber balance)
{
    this->senderBalance = balance;

    calcHash();
}

void Transaction::setReceiverBalance(BigNumber balance)
{
    this->receiverBalance = balance;

    calcHash();
}

void Transaction::setPrevBlock(const BigNumber &value)
{
    this->prevBlock = value;

    calcHash();
}

void Transaction::setGas(int gas)
{
    this->gas = gas;

    calcHash();
}

void Transaction::setHop(int hop)
{
    this->hop = hop;

    calcHash();
}

void Transaction::decrementHop()
{
    this->hop--;

    calcHash();
}

int Transaction::getGas() const
{
    return this->gas;
}

BigNumber Transaction::getSender() const
{
    return this->sender;
}

BigNumber Transaction::getReceiver() const
{
    return this->receiver;
}

BigNumber Transaction::getAmount() const
{
    return this->amount;
}

BigNumber Transaction::getPrevBlock() const
{
    return this->prevBlock;
}

BigNumber Transaction::getSenderBalance() const
{
    return this->senderBalance;
}

BigNumber Transaction::getReceiverBalance() const
{
    return this->receiverBalance;
}

QByteArray Transaction::getHash() const
{
    return this->hash;
}

BigNumber Transaction::getToken() const
{
    return this->token;
}

BigNumber Transaction::getApprover() const
{
    return this->approver;
}

QByteArray Transaction::getData() const
{
    return this->data;
}

QByteArray Transaction::getDigSig() const
{
    return this->digSig;
}

bool Transaction::isEmpty() const
{
    return sender.isEmpty() && receiver.isEmpty() && amount.isEmpty() && data.isEmpty() && prevBlock.isEmpty()
        && approver.isEmpty() && hash.isEmpty();
}

bool Transaction::operator==(const Transaction &transaction) const
{
    if (this->sender != transaction.getSender())
        return false;
    if (this->receiver != transaction.getReceiver())
        return false;
    if (this->amount != transaction.getAmount())
        return false;
    if (this->date != transaction.getDate())
        return false;
    if (this->data != transaction.getData())
        return false;
    if (this->token != transaction.getToken())
        return false;
    if (this->senderBalance != transaction.getSenderBalance())
        return false;
    if (this->receiverBalance != transaction.getReceiverBalance())
        return false;
    if (this->gas != transaction.getGas())
        return false;
    if (this->hop != transaction.getHop())
        return false;
    //    if (this->hash != transaction.getHash())
    //        return false;
    //    if (this->approver != transaction.getApprover())
    //        return false;
    if (this->prevBlock != transaction.getPrevBlock())
        return false;
    //    if (this->digSig != transaction.getDigSig())
    //        return false;
    return true;
}

bool Transaction::operator!=(const Transaction &transaction) const
{
    return !(*this == transaction);
}

void Transaction::operator=(const Transaction &other)
{
    //// to do set up every field by transaction field
    this->sender = other.sender;
    this->receiver = other.receiver;
    this->amount = other.amount;
    this->date = other.date;
    this->data = other.data;
    this->token = other.token;
    this->senderBalance = other.senderBalance;
    this->receiverBalance = other.receiverBalance;
    this->prevBlock = other.prevBlock;
    this->gas = other.gas;
    this->hop = other.hop;
    this->hash = other.hash;
    this->approver = other.approver;
    this->digSig = other.digSig;
}

QString Transaction::toString() const
{
    QStringList list;
    list << "sender:" + sender.toString() << "receiver:" + receiver.toString()
         << "amount:" + amount.toString() << "date:" << QDateTime::fromTime_t(date).toString()
         << "data:" + data << "token:" + token.serialize() << "senderBalance:" + senderBalance.toString()
         << "receiverBalance:" + receiverBalance.toString() << "prevBlock:" + prevBlock.toString()
         << "gas:" + QString::number(gas) << "hop:" + QString::number(hop) << "hash:" + hash
         << "approver:" + approver.toString() << "digitalSignature:" + digSig;
    return Serialization::serializeString(list, Serialization::TX_FIELD_SPLITTER);
}

QByteArray Transaction::serialize() const
{
    QList<QByteArray> list;
    list << sender.toString().toLocal8Bit() << receiver.toString().toLocal8Bit()
         << amount.toString().toLocal8Bit() << QByteArray::number(date) << data << token.toByteArray()
         << senderBalance.toString().toLocal8Bit() << receiverBalance.toString().toLocal8Bit()
         << prevBlock.toByteArray() << QString::number(gas).toLocal8Bit()
         << QString::number(hop).toLocal8Bit() << hash << approver.toString().toLocal8Bit() << digSig;
    //    return Serialization::serialize(list, Serialization::TX_FIELD_SPLITTER);

    return Serialization::universalSerialize(list, FIELS_SIZE);
}
