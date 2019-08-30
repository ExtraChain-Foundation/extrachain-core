#ifndef LIST_CONTAINER_H
#define LIST_CONTAINER_H

#include <QDebug>
#include <QList>
#include <utility>
#include <type_traits>
#include "datastorage/genesis_block.h"
#include "datastorage/actor.h"
#include "datastorage/transaction.h"
#include "datastorage/tx_pair.h"
#include "datastorage/block.h"
#include "utils/utils.h"
#include "utils/bignumber.h"

using std::is_same;

/**
 * This is container for objects that
 * implemetns QByteArray serialize() method and
 * have deserialize constructor T(QByteArray)
 *
 * Serialization pattern:
 * [serializedEntity1][DELIMITER][serializedEntity1][DELIMITER]
 */
template <typename T>
class ListContainer
{
    static_assert (is_same<T, BigNumber>::value ||
                   is_same<T, Transaction>::value ||
                   is_same<T, Actor<KeyPublic>>::value ||
                   is_same<T, Actor<KeyPrivate>>::value ||
                   is_same<T, GenesisDataRow>::value ||
                   is_same<T, TxPair>::value,
                   "Your type is not supported."
                   "Supportable types: BigNumber, Transaction, Block, TxPair, Actor");

private:
    QByteArray delimiter;
    QList<T> dataList;
public:
    ListContainer() : delimiter(Serialization::DEFAULT_LIST_SPLITTER) {}
    ListContainer(const QByteArray &serialized)
    {
        deserialize(serialized);
    }
    ListContainer(const QList<T> &dL, const QByteArray &delimiter)
        : delimiter(delimiter), dataList(dL) {}
    ListContainer(const QList<T> &dL, char delimiter)
        : delimiter(QByteArray(1, delimiter)), dataList(dL) {}
public:
    void add(const T &data)
    {
        dataList.append(data);
    }

    void addAll(const QList<T> &data)
    {
        for (const T &entry : data)
        {
            this->dataList.append(entry);
        }
    }

    void setDelimiter(const QByteArray &delimiter)
    {
        this->delimiter = delimiter;
    }

    void setDelimiter(char delimiter)
    {
        this->delimiter = QByteArray(1, delimiter);
    }

    QList<T> getDataList() const
    {
        return dataList;
    }

    bool isEmpty() const
    {
        return dataList.isEmpty();
    }

    QByteArray serialize() const
    {
        QList<QByteArray> toSerialize;
        for (const T &t: dataList)
        {
            toSerialize.append(t.serialize());
        }
        return Serialization::serialize(toSerialize, delimiter);
    }

    void deserialize(const QByteArray &serialized)
    {
        QList<QByteArray> list = Serialization::deserialize(serialized, delimiter);
        for (const QByteArray &b: list)
        {
            dataList.append(T(b));
        }
    }
};

#endif // LIST_CONTAINER_H
