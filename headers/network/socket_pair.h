#ifndef SOCKET_PAIR_H
#define SOCKET_PAIR_H

#include "utils/bignumber.h"

class SocketPair
{
public:
    std::string ip;
    quint16 port;
    QByteArray iden;
    SocketPair();
    SocketPair(const std::string &f, const quint16 &s);
    SocketPair(const SocketPair &v);
    const QString serialize() const;
    const SocketPair operator=(const SocketPair &v);
    bool operator==(const SocketPair &v) const;
    ~SocketPair();
    BigNumber getId() const;
    void setId(const QByteArray &value);
    bool isEmpty() const;
};

inline uint qHash(const SocketPair &v)
{
    return qHash(v.serialize());
}

QDebug operator<<(QDebug d, const SocketPair &pair);

#endif // SOCKET_PAIR_H
