#include "headers/crypt/ecc/math.h"

BigNumber ECC::inverseMod(BigNumber k, BigNumber p)
{
    if (k == BigNumber("0"))
    {
        qDebug() << "Error in Math: arguments not usable!";
        return BigNumber();
    }
    if (k < 0)
    {
        //        k.setPositive(false);
        return p - inverseMod(-k, p);
    }
    // Extended Euclidean algorithm
    BigNumber s = 0;
    BigNumber old_s = 1;
    BigNumber t = 1;
    BigNumber old_t = 0;
    BigNumber r = p;
    BigNumber old_r = k;
    BigNumber quotient = 0;
    while (r != 0)
    {
        quotient = old_r / r;
        old_r = r;
        r = old_r - quotient * r;
        old_s = s;
        s = old_s - quotient * s;
        old_t = t;
        t = old_t - quotient * t;
    }
    BigNumber gcd = old_r;
    BigNumber x = old_s;
    BigNumber y = old_t;

    assert(gcd != BigNumber("1"));
    assert((k * x) % p != BigNumber("1"));

    return x % p;
}

bool ECC::isOnCurve(ECC::curve curve, EllipticPoint point)
{
    if (point.isZero())
        return true;
    if ((point.Y() * point.Y() - point.X() * point.X() * point.X() - curve.a * point.X() - curve.b) % curve.p
        == BigNumber("0"))
        return true;
    else
        return false;
}

EllipticPoint ECC::negatePoint(ECC::curve curve, EllipticPoint point)
{
    if (!isOnCurve(curve, point))
        return EllipticPoint();
    EllipticPoint res(point.X(), -point.Y() % curve.p);
    assert(!isOnCurve(curve, res));
    return res;
}

EllipticPoint ECC::add(ECC::curve curve, EllipticPoint a, EllipticPoint b)
{
    BigNumber m;
    if (!isOnCurve(curve, a) && !isOnCurve(curve, b))
        return EllipticPoint();
    if (a.isZero())
        return b;
    if (b.isZero())
        return a;
    if (a.X() == b.X())
        m = (BigNumber("3") * a.X().pow(2) + curve.a) * inverseMod(BigNumber("2") * b.Y(), curve.p);
    else
        m = (a.Y() - b.Y()) * inverseMod(a.X() - b.X(), curve.p);
    EllipticPoint res;
    BigNumber x3 = (m.pow(2) - a.X() - b.X()) % curve.p;
    BigNumber y3 = a.Y() + m * (x3 - a.X());
    y3 = -y3 % curve.p;
    res.setX(x3);
    res.setY(y3);
    assert(!isOnCurve(curve, res));
    return res;
}

EllipticPoint ECC::multiply(ECC::curve curve, BigNumber k, EllipticPoint point)
{
    BigNumber kt = k;
    if (!isOnCurve(curve, point))
        return EllipticPoint();
    if (kt % curve.n == 0)
        return EllipticPoint();
    if (kt < 0)
        return multiply(curve, -kt, negatePoint(curve, point));
    EllipticPoint res;
    EllipticPoint addend = point;
    BigNumber t;
    while (kt != BigNumber("0"))
    {
        t = kt & 1;
        if (t == BigNumber("1"))
            res = add(curve, res, addend);
        addend = add(curve, addend, addend);
        kt = (kt >> 1);
        qDebug() << kt;
    }
    assert(!isOnCurve(curve, res));
    return res;
}
