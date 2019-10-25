#include "network/socket_pair.h"

SocketPair::SocketPair(QObject *parent)
    : QObject(parent)
{

    id = "0";
    first = "0.0.0.0";
    second = 0;
}

SocketPair::SocketPair(const std::string &f, const quint16 &s, QObject *parent)
    : QObject(parent)
{
    first = f;
    second = s;

    id = "0";
}

SocketPair::SocketPair(const SocketPair &v, QObject *parent)
    : QObject(parent)
{
    first = v.first;
    second = v.second;

    id = v.id;
}

SocketPair::~SocketPair()
{
}

const QString SocketPair::serialize() const
{
    return QString::fromStdString(first) + QString::number(second) + QString(id);
}

const SocketPair SocketPair::operator=(const SocketPair &v)
{
    first = v.first;
    second = v.second;
    id = v.id;
    return this;
}

bool SocketPair::operator==(const SocketPair &v) const
{
    return ((first == v.first) && (second == v.second) && (id == v.id));
}

BigNumber SocketPair::getId() const
{
    return id;
}

void SocketPair::setId(const QByteArray &value)
{
    id = value;
}

bool SocketPair::isEmpty() const
{
    if ((first == "0.0.0.0") && (second == 0) && (id == "0"))
        return true;
    else
        return false;
}
