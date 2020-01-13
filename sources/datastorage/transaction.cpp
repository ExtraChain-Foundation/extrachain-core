#include "datastorage/transaction.h"

Transaction::Transaction(QObject *parent)
{
    this->sender = BigNumber(0);
    this->receiver = BigNumber(0);
    this->amount = BigNumber(0);
    this->date = QDateTime::currentDateTime().toTime_t();
    this->data = QByteArray();
    this->token = BigNumber(0);
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
    QList<QByteArray> list =
        Serialization::universalDeserialize(serialized, Serialization::TRANSACTION_FIELD_SIZE);
    if (list.size() == 12)
    {
        this->sender = BigNumber(list.at(0));
        this->receiver = BigNumber(list.at(1));
        this->amount = BigNumber(list.at(2));
        this->date = list.at(3).toLongLong();
        this->data = list.at(4);
        this->token = BigNumber(list.at(5));
        this->prevBlock = BigNumber(list.at(6));
        this->gas = list.at(7).toInt();
        this->hop = list.at(8).toInt();
        this->hash = QByteArray(list.at(9));
        this->approver = BigNumber(list.at(10));
        this->digSig = list.at(11);
    }
    else
        qDebug() << "Incorrect TX";

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
    return (sender.toActorId() + receiver.toActorId() + amount.toByteArray() + QByteArray::number(date) + data
            + token.toActorId() + prevBlock.toByteArray() + QByteArray::number(gas) + approver.toActorId());
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
    this->sender = other.sender;
    this->receiver = other.receiver;
    this->amount = other.amount;
    this->date = other.date;
    this->data = other.data;
    this->token = other.token;
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
    list << "sender:" + sender.toActorId() << "receiver:" + receiver.toActorId()
         << "amount:" + amount.toByteArray() << "date:" << QDateTime::fromTime_t(date).toString()
         << "data:" + data << "token:" + token.toActorId() << "prevBlock:" + prevBlock.toByteArray()
         << "gas:" + QString::number(gas) << "hop:" + QString::number(hop) << "hash:" + hash
         << "approver:" + approver.toActorId() << "digitalSignature:" + digSig;
    return Serialization::serializeString(list, Serialization::TX_FIELD_SPLITTER);
}

QByteArray Transaction::serialize() const
{
    QList<QByteArray> list;
    list << sender.toActorId() << receiver.toActorId() << amount.toByteArray() << QByteArray::number(date)
         << data << token.toActorId() << prevBlock.toByteArray() << QString::number(gas).toLocal8Bit()
         << QString::number(hop).toLocal8Bit() << hash << approver.toActorId() << digSig;
    //    return Serialization::serialize(list, Serialization::TX_FIELD_SPLITTER);

    return Serialization::universalSerialize(list, Serialization::TRANSACTION_FIELD_SIZE);
}

BigNumber Transaction::visibleToAmount(QByteArray amount)
{
    amount += amount.indexOf(".") == -1 ? "." : "";
    QByteArrayList amountList = amount.split('.');
    int secondLength = amountList[1].length();

    amount += QString("0").repeated(18 - secondLength).toLatin1();
    amount.replace(".", "");

    return BigNumber(amount, 10);
}

QString Transaction::amountToVisible(BigNumber number)
{
    if (number == 0)
        return "0";

    QByteArray numberArr = number.toByteArray(10);
    QString second = numberArr.right(18); //
    second = QString("0").repeated(18 - second.length()).toLatin1() + second;
    second = second.remove(QRegExp("[0]*$"));
    QByteArray first = numberArr.left(numberArr.length() - 18);

    QString numberDec =
        (first.isEmpty() ? "0" : first) + (second == "0" || second.isEmpty() ? "" : "." + second);

    return numberDec.toLatin1();
}
