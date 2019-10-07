#include "EllipticPoints.h"
EllipticPoints::EllipticPoints()
{
    this->x = 0;
    this->y = 0;
}
EllipticPoints::EllipticPoints(BigNumber x)
{
    *this = calcPointOnCurve(x);
}

EllipticPoints::EllipticPoints(BigNumber pointX, BigNumber pointY)
{
    this->x = pointX;
    this->y = pointY;
}

EllipticPoints::EllipticPoints(const EllipticPoints &point)
{
    this->x = point.x;
    this->y = point.y;
}

bool EllipticPoints::checkOnCurve(BigNumber x, BigNumber y)
{
    // y^2 = x^3 + ax + b mod p
    return (y * y) % Curves::pCurve == (x * x * x + Curves::aCurve * x + Curves::bCurve) % Curves::pCurve;
    return y == (BigNumber::sqrt(x * x * x + Curves::aCurve * x + Curves::bCurve) % Curves::pCurve);
}

EllipticPoints EllipticPoints::calcPointOnCurve(BigNumber x)
{
    BigNumber cur_x = x % Curves::pCurve;
    return EllipticPoints(cur_x,
                          BigNumber::sqrt(cur_x * cur_x * cur_x + Curves::aCurve * cur_x + Curves::bCurve)
                              % Curves::pCurve);
}

EllipticPoints EllipticPoints::operator*(const BigNumber &bigNumber)
{
    return EllipticPoints(this->x * bigNumber, this->y * bigNumber);
}

BigNumber EllipticPoints::operator*(const EllipticPoints &point)
{
    return (x * point.x + y * point.y);
}

EllipticPoints EllipticPoints::operator+(EllipticPoints &point)
{
    // return EllipticPoints((this->getX() + point.x), (this->getY() + point.y));
    BigNumber lambda;
    BigNumber x3;
    BigNumber y3;
    if (this->x == point.x)
    {
        if (point.y != BigNumber("0"))
            this->y = -point.y;
        lambda = (this->x * this->x * 3 + Curves::aCurve * 2 * this->x) / (this->y * 2);
        x3 = lambda * lambda - this->x - point.x;
        y3 = -this->y - lambda * (x3 - this->x);
    }
    else
    {
        lambda = (point.y - this->y) / (point.x - this->x);
        x3 = lambda * lambda - this->x - point.x;
        y3 = -this->y - lambda * (x3 - this->x);
    }
    return EllipticPoints(x3, y3);
}

QByteArray EllipticPoints::CryptMessage(QByteArray message)
{
    QByteArray resMessage = "";
    QByteArray cryptKey = this->x.toByteArray();
    cryptKey.append(this->y.toByteArray());
    if (message.size() < cryptKey.size())
    {
        for (int i = 0; i < message.size(); i++)
            resMessage.append((char)(message[i] ^ cryptKey[i]));
        return resMessage;
    }
    else if (message.size() > cryptKey.size())
    {
        for (int i = 0; i < cryptKey.size() - message.size(); i++)
            cryptKey.append("0");
        for (int i = 0; i < message.size(); i++)
            resMessage.append((char)(message[i] ^ cryptKey[i]));
        return resMessage;
    }

    for (int i = 0; i < message.size(); i++)
        resMessage.append((char)(message[i] ^ cryptKey[i]));

    return resMessage;

    //    for(int i=0;i<message.size();i++)
    //    {
    //        QByteArray temp="";
    //        for(int j=0;j<this->x.toByteArray().size();j++)
    //        {
    //            temp.append((char)(message[i]^this->x.toByteArray()[j]));
    //        }
    //        resMessage.append(temp);
    //        temp="";
    //        for(int j=0;j<this->y.toByteArray().size();j++)
    //        {
    //            temp.append((char)(message[i]^this->y.toByteArray()[j]));
    //        }
    //        resMessage.append(temp);
    //    }
}
