#include "datastorage/tx_pair.h"

TxPair::TxPair()
{

}

TxPair::TxPair(const QByteArray &serialized)
{
    QList<QByteArray> list = Serialization::deserialize(
                serialized, Serialization::TX_PAIR_FIELD_SPLITTER);
    if (list.size() == 2)
    {
        first = Transaction(list.at(0));
        second = Transaction(list.at(1));
    }
}

QByteArray TxPair::serialize() const
{
     QList<QByteArray> list;
     list << first.serialize()
          << second.serialize();
     return Serialization::serialize(list, Serialization::TX_PAIR_FIELD_SPLITTER);
}

bool TxPair::operator==(const TxPair &other) const
{
   if(this->first != other.first)
       return false;
   if(this->second != other.second)
       return false;
   return true;
}

Transaction TxPair::getFirst() const
{
    return first;
}

void TxPair::setFirst(const Transaction &value)
{
    first = value;
}

Transaction TxPair::getSecond() const
{
    return second;
}

void TxPair::setSecond(const Transaction &value)
{
    second = value;
}

bool TxPair::isEmpty() const
{
    return first.isEmpty() && second.isEmpty();
}
