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

/**
 * @brief Contract
 */

Contract::performer_status Contract::fromInt(const int &s) const
{
    if (s == 1)
        return performer_status::Approve;
    else if (s == 2)
        return performer_status::Done;
    else
        return performer_status::notApprove;
}

const QMap<BigNumber, Contract::performer_status>
Contract::performersDeserialize(const QByteArray &serialized) const
{
    QMap<BigNumber, performer_status> result;
    QList<QByteArray> list = Serialization::deserialize(serialized, PERFORMER_LIST_DELIMETR);
    for (const QByteArray &el : list)
    {
        QList<QByteArray> list_el = Serialization::deserialize(el, PERFORMER_DELIMETR);
        if (list.size() != 2)
        {
            qDebug() << "[Contract] performer fields list wrong size";
            break;
        }
        result[BigNumber(list_el.takeFirst())] = fromInt(list_el.takeFirst().toInt());
    }
    return result;
}

const QByteArray Contract::performersSerialize() const
{
    QList<QByteArray> list;
    //    std::for_each(performers.begin(), performers.end(),
    //                  [&list, this](QMap<BigNumber, performer_status>::const_iterator it) {
    //                      QByteArray list_el = Serialization::serialize(
    //                          { it.key().toByteArray(), QByteArray::number(it.value()) },
    //                          PERFORMER_DELIMETR);
    //                      list << list_el;
    //                  });
    return Serialization::serialize(list, PERFORMER_LIST_DELIMETR);
}

const QByteArrayList Contract::_scope_of_workDeserialize(const QByteArray &serialized) const
{
    return Serialization::deserialize(serialized, SCOPE_OF_WORK_DELIMETR);
}

const QByteArray Contract::_scope_of_workSerialize() const
{
    return Serialization::serialize(_scope_of_work, SCOPE_OF_WORK_DELIMETR);
}

const QPair<long long, long long> Contract::_contract_dateDeserialize(const QByteArray &serialized) const
{
    QList<QByteArray> list = Serialization::deserialize(serialized, CONTRACT_DATE_DELIMETR);
    return QPair<long long, long long>(list.takeFirst().toLongLong(), list.takeFirst().toLongLong());
}

const QByteArray Contract::_contract_dateSerialize() const
{
    return Serialization::serialize(
        { QByteArray::number(_contract_date.first), QByteArray::number(_contract_date.second) },
        CONTRACT_DATE_DELIMETR);
}

Contract::Contract(const QByteArray &serialize_contract, QObject *parent)
    : QObject(parent)
{
    QList<QByteArray> list = deserialize(serialize_contract);
    if (list.isEmpty())
        return;
    customer = BigNumber(list.takeFirst());
    performers = performersDeserialize(list.takeFirst());
    _location = list.takeFirst();
    event = list.takeFirst();
    _contract_date = _contract_dateDeserialize(list.takeFirst());
    _scope_of_work = _scope_of_workDeserialize(list.takeFirst());
    _agreement = list.takeFirst();
    _amount = BigNumber(list.takeFirst());
    data = list.takeFirst();
}

Contract::Contract(const Contract &contract, QObject *parent)
    : QObject(parent)
{
    customer = contract.customer;
    performers = contract.performers;
    _location = contract._location;
    event = contract.event;
    _contract_date = contract._contract_date;
    _scope_of_work = contract._scope_of_work;
    _agreement = contract._agreement;
    _amount = contract._amount;
    data = contract.data;
}

Contract::Contract(const BigNumber &_customer, const QMap<BigNumber, performer_status> &_performers,
                   const QByteArray &_location, const QByteArray &event,
                   const QPair<long long, long long> &_contract_date, const QList<QByteArray> &_scope_of_work,
                   const QByteArray &_agreement, const BigNumber &_amount, QObject *parent)
    : QObject(parent)
    , customer(_customer)
    , performers(_performers)
    , _location(_location)
    , event(event)
    , _contract_date(_contract_date)
    , _scope_of_work(_scope_of_work)
    , _agreement(_agreement)
    , _amount(_amount)
{
}

const QByteArray Contract::serialize() const
{
    QList<QByteArray> list;
    list << customer.toActorId() << performersSerialize() << _location << event << _contract_dateSerialize()
         << _scope_of_workSerialize() << _agreement << _amount.toByteArray() << data;
    return Serialization::universalSerialize(list, CONTRACT_FIELDS_SIZE);
}

const QList<QByteArray> Contract::deserialize(const QByteArray &serialized) const
{
    return Serialization::universalDeserialize(serialized, CONTRACT_FIELDS_SIZE);
}
