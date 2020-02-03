#include "network/socket_pair.h"

SocketPair::SocketPair()
{

    iden = "0";
    ip = "0.0.0.0";
    port = 0;
}

SocketPair::SocketPair(const std::string &f, const quint16 &s)
{
    ip = f;
    port = s;

    iden = "0";
}

SocketPair::SocketPair(const SocketPair &v)
{
    ip = v.ip;
    port = v.port;

    iden = v.iden;
}

SocketPair::~SocketPair()
{
}

const QString SocketPair::serialize() const
{
    return QString::fromStdString(ip) + QString::number(port) + QString(iden);
}

const SocketPair SocketPair::operator=(const SocketPair &v)
{
    ip = v.ip;
    port = v.port;
    iden = v.iden;
    return *this;
}

bool SocketPair::operator==(const SocketPair &v) const
{
    return ((ip == v.ip) && (port == v.port) && (iden == v.iden));
}

BigNumber SocketPair::getId() const
{
    return iden;
}

void SocketPair::setId(const QByteArray &value)
{
    iden = value;
}

bool SocketPair::isEmpty() const
{
    if ((ip == "0.0.0.0") && (port == 0) && (iden == "0"))
        return true;
    else
        return false;
}
