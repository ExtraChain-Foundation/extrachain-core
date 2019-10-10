#include "datastorage/contract.h"
//#include "utils/utils.h"
Token::Token(QObject *parent)
    : QObject(parent)
{
}
Token::Token(BigNumber actorId, QObject *parent)
    : Token(parent)
{
    this->actorId = actorId;
}

BigNumber Token::getPrice() const
{
    return price;
}

BigNumber Token::calcActivity()
{
    QByteArray res;

    return res;
}

BigNumber Token::calcFunds()
{
    BigNumber res;
    res = getWorkDone() / getDaysGone();
    return res;
}

BigNumber Token::calcSum()
{
    BigNumber res;
    res = this->getPt() / this->getWorkDone();
    return res;
}

BigNumber Token::getActorId() const
{
    return actorId;
}
QList<QByteArray> Token::getPathtoAllBlocks()
{
    //  QList<QByteArray> res;
    QList<QByteArray> path;
    QDir dir = (DataStorage::BLOCKCHAIN_INDEX + DataStorage::BLOCK_INDEX_FOLDER_NAME); // Valik check it!)
    QFileInfoList list = dir.entryInfoList();
    for (int i = 0; i < list.size(); ++i)
    {
        QFileInfo fileInfo = list.at(i);
        if (('0' < fileInfo.fileName().at(0)) && (fileInfo.fileName().at(0) < '9'))
            path.append(fileInfo.fileName().toUtf8());
    }

    for (int count = 0; count < path.size(); count++)
    {
        //      res.append( DataStorage::BLOCKCHAIN_INDEX.toUtf8() +
        //      DataStorage::BLOCK_INDEX_FOLDER_NAME.toUtf8() + "/" + path.at(count) );
        path.replace(count,
                     DataStorage::BLOCKCHAIN_INDEX.toUtf8() + DataStorage::BLOCK_INDEX_FOLDER_NAME.toUtf8()
                         + "/" + path.at(count));
    }
    return path;
}
BigNumber Token::calcRating()
{
    return this->calcActivity() * this->calcFunds() * this->calcSum();
}

BigNumber Token::getWorkDone()
{
    return this->WorkDone;
}

BigNumber Token::getDaysGone()
{
    return this->DaysGone;
}

BigNumber Token::getSum()
{
    return this->Sum;
}

BigNumber Token::getPt()
{
    return this->Pt;
}

BigNumber Token::getAmount(QList<Block> list, BigNumber userId)
{
    BigNumber amount;

    for (int count = list.size() - 1; count >= 0; count--)
    {
        list.at(count).extractTransactions();
        QList<Transaction> trList;
        // BigNumber amount;
        trList.append(list.at(count).extractTransactions());
        if (trList.at(count).getReceiver() == userId)
            amount += trList.at(count).getAmount();
    }

    return amount;
}

// QByteArray Contract::getCustomer_sign() const
//{
//    return customer_sign;
//}

// void Contract::setCustomer_sign(const QByteArray &value)
//{
//    customer_sign = value;
//}

// QByteArray Contract::getPerformer_sign() const
//{
//    return performer_sign;
//}

// void Contract::setPerformer_sign(const QByteArray &value)
//{
//    performer_sign = value;
//}

// QByteArray Contract::calcDigSig(const Actor<KeyPrivate> &actor)
//{
//    return actor.getKey()->sign(getSignData()).toBase64();
//}

// bool Contract::verifyDigSig(const Actor<KeyPublic> &actor, const QByteArray &data, const QByteArray
// &digSig)
//{
//    return actor.getKey()->verify(getSignData(), QByteArray::fromBase64(digSig));
//}

// BigNumber Contract::getCustomer() const
//{
//    return customer;
//}

// void Contract::setCustomer(const BigNumber &value)
//{
//    customer = value;
//}

// BigNumber Contract::getPerformer() const
//{
//    return performer;
//}

// void Contract::setPerformer(const BigNumber &value)
//{
//    performer = value;
//}

// bool Contract::checkCustomerSign(const Actor<KeyPublic> &actor)
//{
//    if (customer_sign.isEmpty() || !verifyDigSig(actor, getSignData(), customer_sign))
//    {
//    }
//    return true;
//}

// bool Contract::checkPerformerSign(const Actor<KeyPublic> &actor)
//{
//    if (performer_sign.isEmpty() || !verifyDigSig(actor, getSignData(), performer_sign))
//    {
//    }
//    return true;
//}

// QByteArray Contract::getLocation() const
//{
//    return location;
//}

// void Contract::setLocation(const QByteArray &value)
//{
//    location = value;
//}

// QByteArray Contract::getEvent() const
//{
//    return event;
//}

// void Contract::setEvent(const QByteArray &value)
//{
//    event = value;
//}

// QPair<long long, long long> Contract::getEvent_date() const
//{
//    return event_date;
//}

// void Contract::setEvent_date(const QPair<long long, long long> &value)
//{
//    event_date = value;
//}

// QList<QByteArray> Contract::getScope_of_work() const
//{
//    return scope_of_work;
//}

// void Contract::setScope_of_work(const QList<QByteArray> &value)
//{
//    scope_of_work = value;
//}

// QByteArray Contract::getAgreement() const
//{
//    return agreement;
//}

// void Contract::setAgreement(const QByteArray &value)
//{
//    agreement = value;
//}

// BigNumber Contract::getAmount() const
//{
//    return amount;
//}

// void Contract::setAmount(const BigNumber &value)
//{
//    amount = value;
//}

// QByteArray Contract::getHash() const
//{
//    return Utils::calcKeccak(getSignData());
//}

// QByteArray Contract::getFileName() const
//{
//    QList<QByteArray> data = { customer.serialize(),
//                               performer.serialize(),
//                               location,
//                               event,
//                               QString::number(event_date.first).toUtf8(),
//                               QString::number(event_date.second).toUtf8() };
//    data.append(scope_of_work);
//    data.append(agreement);
//    data.append(amount.serialize());
//    return Utils::calcKeccak(Serialization::universalSerialize(data, Serialization::DEFAULT_FIELD_SIZE));
//}

// bool Contract::makeFirstTransction() const
//{
//    if (!first_transaction_hash.isEmpty())
//        return false;
//    if (!customer_sign.isEmpty() && !performer_sign.isEmpty())
//    {
//        return true;
//    }
//    return false;
//}

// bool Contract::makeFinalTransaction() const
//{
//    if (isCompleted)
//    {
//        return false;
//    }
//    if (first_transaction_hash.isEmpty())
//    {
//        return false;
//    }
//    if (approve_complete_customer && approve_complete_performer && final_transaction_hash.isEmpty())
//    {
//        return true;
//    }
//    return false;
//}

// QByteArray Contract::getFirst_transaction_hash() const
//{
//    return first_transaction_hash;
//}

// void Contract::setFirst_transaction_hash(const QByteArray &value)
//{
//    first_transaction_hash = value;
//}

// QByteArray Contract::getFinal_transaction_hash() const
//{
//    return final_transaction_hash;
//}

// void Contract::setFinal_transaction_hash(const QByteArray &value)
//{
//    final_transaction_hash = value;
//}

// bool Contract::getApprove_complete_performer() const
//{
//    return approve_complete_performer;
//}

// void Contract::setApprove_complete_performer(bool value)
//{
//    approve_complete_performer = value;
//}

// void Contract::completeContractByCustomer()
//{
//    approve_complete_customer = true;
//    approve_complete_performer = true;

//    isCompleted = true;
//}

// void Contract::completeContractByPerformer()
//{
//    approve_complete_customer = true;
//}

// bool Contract::getIsCompleted() const
//{
//    return isCompleted;
//}

// void Contract::setIsCompleted(bool value)
//{
//    isCompleted = value;
//}

// Contract::Contract(QObject *parent)
//    : QObject(parent)
//{
//}

// Contract::Contract(const QByteArray serialize_contract, QObject *parent)
//    : QObject(parent)
//{
//    QList<QByteArray> data =
//        Serialization::universalDesirialize(serialize_contract, Serialization::DEFAULT_FIELD_SIZE);

//    customer = BigNumber(data.at(0));
//    performer = BigNumber(data.at(1));
//    location = data.at(2);
//    event = data.at(3);
//    event_date = qMakePair(data.at(4).toLongLong(), data.at(5).toLongLong());
//    scope_of_work = Serialization::universalDesirialize(data.at(6), Serialization::DEFAULT_FIELD_SIZE);
//    agreement = data.at(7);
//    amount = BigNumber(data.at(8));
//    customer_sign = data.at(9);
//    performer_sign = data.at(10);

//    first_transaction_hash = data.at(11);
//    final_transaction_hash = data.at(12);

//    approve_complete_customer = data.at(13).toInt();
//    approve_complete_performer = data.at(14).toInt();

//    isCompleted = data.at(15).toInt();
//}

// Contract::Contract(const Contract &contract, QObject *parent)
//    : QObject(parent)
//{
//    customer = contract.customer;
//    performer = contract.performer;
//    location = contract.location;
//    event = contract.event;
//    event_date = contract.event_date;
//    scope_of_work = contract.scope_of_work;
//    agreement = contract.agreement;
//    amount = contract.amount;
//    customer_sign = contract.customer_sign;
//    performer_sign = contract.performer_sign;

//    first_transaction_hash = contract.first_transaction_hash;
//    final_transaction_hash = contract.final_transaction_hash;

//    approve_complete_customer = contract.approve_complete_customer;
//    approve_complete_performer = contract.approve_complete_performer;

//    isCompleted = contract.isCompleted;
//}

// Contract::Contract(const BigNumber _customer, const BigNumber _performer, const QByteArray _location,
//                   const QByteArray _event, const QPair<long long, long long> _event_date,
//                   const QList<QByteArray> _scope_of_work, const QByteArray _agreement,
//                   const BigNumber _amount, QObject *parent)
//{
//    (void)parent;
//    customer = _customer;
//    performer = _performer;
//    location = _location;
//    event = _event;
//    event_date = _event_date;
//    scope_of_work = _scope_of_work;
//    agreement = _agreement;
//    amount = _amount;

//    approve_complete_customer = false;
//    approve_complete_performer = false;
//    isCompleted = false;
//}

// Contract Contract::operator=(const Contract &contract)
//{
//    customer = contract.customer;
//    performer = contract.performer;
//    location = contract.location;
//    event = contract.event;
//    event_date = contract.event_date;
//    scope_of_work = contract.scope_of_work;
//    agreement = contract.agreement;
//    amount = contract.amount;
//    customer_sign = contract.customer_sign;
//    performer_sign = contract.performer_sign;

//    first_transaction_hash = contract.first_transaction_hash;
//    final_transaction_hash = contract.final_transaction_hash;

//    approve_complete_customer = contract.approve_complete_customer;
//    approve_complete_performer = contract.approve_complete_performer;

//    isCompleted = contract.isCompleted;
//    return *this;
//}

// bool Contract::operator==(const Contract &contract) const
//{
//    if (customer != contract.customer || performer != contract.performer || location != contract.location
//        || event != contract.event || event_date != contract.event_date
//        || scope_of_work != contract.scope_of_work || agreement != contract.agreement
//        || amount != contract.amount || customer_sign != contract.customer_sign
//        || performer_sign != contract.performer_sign ||

//        approve_complete_customer != contract.approve_complete_customer
//        || approve_complete_performer != contract.approve_complete_performer
//        || isCompleted != contract.isCompleted)
//        return false;
//    return true;
//}

// QByteArray Contract::serialize() const
//{
//    QList<QByteArray> data = { customer.serialize(),
//                               performer.serialize(),
//                               location,
//                               event,
//                               QString::number(event_date.first).toUtf8(),
//                               QString::number(event_date.second).toUtf8(),
//                               Serialization::universalSerialize(scope_of_work,
//                                                                 Serialization::DEFAULT_FIELD_SIZE),
//                               agreement,
//                               amount.serialize(),
//                               customer_sign,
//                               performer_sign,
//                               first_transaction_hash,
//                               final_transaction_hash,
//                               QByteArray::number(approve_complete_customer),
//                               QByteArray::number(approve_complete_performer),
//                               QByteArray::number(isCompleted) };
//    return Serialization::universalSerialize(data, Serialization::DEFAULT_FIELD_SIZE);
//}

// QByteArray Contract::getSignData() const
//{
//    QList<QByteArray> data = { customer.serialize(),
//                               performer.serialize(),
//                               location,
//                               event,
//                               QString::number(event_date.first).toUtf8(),
//                               QString::number(event_date.second).toUtf8() };
//    data.append(scope_of_work);
//    data.append({ agreement, amount.serialize(), first_transaction_hash,
//                  QByteArray::number(approve_complete_customer),
//                  QByteArray::number(approve_complete_performer) });
//    return Serialization::universalSerialize(data, Serialization::DEFAULT_FIELD_SIZE);
//}

// void Contract::signByCustomer(const Actor<KeyPrivate> &actor)
//{
//    setCustomer_sign(calcDigSig(actor));
//}

// void Contract::signByPerformer(const Actor<KeyPrivate> &actor)
//{
//    setPerformer_sign(calcDigSig(actor));
//}
