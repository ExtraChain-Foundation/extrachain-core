#include "datastorage/transaction.h"

Transaction::Transaction(QObject *parent)
{
    this->sender = BigNumber(0);
    this->receiver = BigNumber(0);
    this->amount = BigNumber(0);
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = QByteArray();
    this->token = BigNumber(0);
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->approver = BigNumber(0);
    this->digSig = QByteArray();
    this->producer = BigNumber(0);
    calcHash();
}

Transaction::Transaction(const QByteArray &serialized, QObject *parent)
    : Transaction()
{
    //    QList<QByteArray> list =
    //        Serialization::deserialize(serialized, Serialization::TX_FIELD_SPLITTER);
    QList<QByteArray> list =
        Serialization::deserialize(serialized, Serialization::TRANSACTION_FIELD_SIZE);
    if (list.size() == 13)
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
        this->producer = BigNumber(list.at(12));
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
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = QByteArray();
    this->token = BigNumber(0);
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->approver = BigNumber(0);
    this->digSig = QByteArray();
    this->producer = BigNumber(0);
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
    this->producer = other.producer;
    calcHash();
}

void Transaction::setReceiver(const BigNumber &value)
{
    receiver = value;
}

void Transaction::setProducer(const BigNumber &value)
{
    producer = value;
}

void Transaction::setSender(const BigNumber &value)
{
    sender = value;
}

BigNumber Transaction::getProducer() const
{
    return producer;
}

void Transaction::setAmount(const BigNumber &value)
{
    amount = value;
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
            + token.toActorId() + prevBlock.toByteArray() + QByteArray::number(gas) + approver.toActorId()
            + producer.toActorId());
}

QByteArray Transaction::getDataForDigSig() const
{
    return getDataForHash() + hash;
}

void Transaction::sign(const Actor<KeyPrivate> &actor)
{
    this->approver = actor.id();
    calcHash();
    this->digSig = actor.key()->sign(getDataForDigSig());
}

bool Transaction::verify(const Actor<KeyPublic> &actor) const
{
    return digSig.isEmpty() ? false : actor.key()->verify(getDataForDigSig(), getDigSig());
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

void Transaction::clear()
{
    this->sender = BigNumber(0);
    this->receiver = BigNumber(0);
    this->amount = BigNumber(0);
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = QByteArray();
    this->token = BigNumber(0);
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->approver = BigNumber(0);
    this->digSig = QByteArray();
    this->producer = BigNumber(0);
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
    this->producer = other.producer;
}

QString Transaction::toString() const
{
    return "sender:" + sender.toActorId() + ", receiver:" + receiver.toActorId()
        + ", amount:" + amount.toByteArray() + ", date:" + QDateTime::fromMSecsSinceEpoch(date).toString()
        + ", data:" + data + ", token:" + token.toActorId() + ", prevBlock:" + prevBlock.toByteArray()
        + ", gas:" + QString::number(gas) + ", hop:" + QString::number(hop) + ", hash:" + hash
        + ", approver:" + approver.toActorId() + ", digitalSignature:" + digSig;
}

QByteArray Transaction::serialize() const
{
    QList<QByteArray> list;
    list << sender.toActorId() << receiver.toActorId() << amount.toByteArray() << QByteArray::number(date)
         << data << token.toActorId() << prevBlock.toByteArray() << QString::number(gas).toLocal8Bit()
         << QString::number(hop).toLocal8Bit() << hash << approver.toActorId() << digSig
         << producer.toActorId();
    //    return Serialization::serialize(list, Serialization::TX_FIELD_SPLITTER);

    return Serialization::serialize(list, Serialization::TRANSACTION_FIELD_SIZE);
}

BigNumber Transaction::visibleToAmount(QByteArray amount)
{
    if (amount.isEmpty())
        return 0;

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
    bool minus = false;

    if (numberArr[0] == '-')
    {
        numberArr = numberArr.remove(0, 1);
        minus = true;
    }

    QString second = numberArr.right(18); //
    second = QString("0").repeated(18 - second.length()).toLatin1() + second;
    second = second.remove(QRegExp("[0]*$"));
    QByteArray first = numberArr.left(numberArr.length() - 18);

    QString numberDec =
        (first.isEmpty() ? "0" : first) + (second == "0" || second.isEmpty() ? "" : "." + second);

    return (minus ? "-" : "") + numberDec.toLatin1();
}

BigNumber Transaction::amountNormalizeMul(BigNumber number)
{
    QByteArray n = number.toByteArray(10);
    if (n.length() < 36)
        return number;
    return BigNumber(n.chopped(18), 10);
}

BigNumber Transaction::amountMul(BigNumber number1, BigNumber number2)
{
    QByteArray one = Transaction::amountToVisible(number1).toLatin1();
    QByteArray two = Transaction::amountToVisible(number1).toLatin1();
    int index1 = one.indexOf(".");
    int index2 = two.indexOf(".");
    int div1 = one.size() - index1 - 1;
    int div2 = two.size() - index2 - 1;
    BigNumber returned1 = index1 == -1 ? 1 : BigNumber(10).pow(div1);
    BigNumber returned2 = index2 == -1 ? 1 : BigNumber(10).pow(div2);

    BigNumber number = (number1 * returned1) * (number2 * returned2);

    return amountNormalizeMul(number) / returned1 / returned2;
}

BigNumber Transaction::amountDiv(BigNumber number1, BigNumber number2)
{
    QByteArray two = Transaction::amountToVisible(number2).toLatin1();
    int index = two.indexOf(".");
    int div = two.size() - index - 1;
    QByteArray newTwoByte = two.remove(index, 1);

    BigNumber returned = index == -1 ? 1 : BigNumber(10).pow(div);
    auto second = BigNumber(newTwoByte, 10);
    if (second == 0)
        return 0;

    return number1 * returned / second;
}

BigNumber Transaction::amountPercent(BigNumber number, uint percent)
{
    if (percent > 100)
        percent = 100;
    return number * percent / 100;
}
