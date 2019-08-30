#ifndef TX_PAIR_H
#define TX_PAIR_H

#include <QByteArray>
#include "datastorage/transaction.h"

/**
 * @brief The container for Transaction pair
 */
class TxPair
{
private:
    Transaction first;
    Transaction second;

public:
    TxPair();
    TxPair(const QByteArray &serialized);

public:
    Transaction getFirst() const;
    Transaction getSecond() const;
    void setFirst(const Transaction &value);
    void setSecond(const Transaction &value);
    bool isEmpty() const;
    QByteArray serialize() const;
    bool operator==(const TxPair &other) const;
};

inline bool operator<(const TxPair &l, const TxPair &r)
{
    return l.getFirst() < l.getSecond() || r.getFirst() < l.getSecond();
}

#endif // TX_PAIR_H
