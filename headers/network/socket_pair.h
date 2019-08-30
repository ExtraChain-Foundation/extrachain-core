#ifndef SOCKET_PAIR_H
#define SOCKET_PAIR_H

#include "utils/bignumber.h"

class SocketPair : public QObject
{
    Q_OBJECT

public:
    std::string first;
    quint16 second;
    QByteArray id;
    SocketPair(QObject *parent = nullptr);
    SocketPair(const std::string &f, const quint16 &s, QObject *parent = nullptr);
    SocketPair(const SocketPair &v, QObject *parent = nullptr);
    const QString serialize() const;
    const SocketPair operator=(const SocketPair &v);
    bool operator==(const SocketPair &v) const;
    ~SocketPair();
    BigNumber getId() const;
    void setId(const QByteArray &value);
};

inline uint qHash(const SocketPair &v)
{
    return qHash(v.serialize());
}

#endif // SOCKET_PAIR_H
