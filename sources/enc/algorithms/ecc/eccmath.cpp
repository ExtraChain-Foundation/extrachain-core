#include "enc/algorithms/ecc/eccmath.h"

BigNumber ECC::inverseMod(BigNumber a, BigNumber b)
{
    BigNumber an = a;
    BigNumber bn = b;
    if (a < 0)
        return b - inverseMod(-a, b);
    if (a < 0)
        return b - inverseMod(-a, b);
    assert(a != 0);
    BigNumber b0 = b, t, q;

    BigNumber x0 = 0, x1 = 1;
    if (b == 1)
        return 1;

    while (a > 1)
    {
        q = a / b;
        t = b;
        b = a % b;
        a = t;
        t = x0;
        x0 = x1 - q * x0;
        x1 = t;
    }
    if (x1 < 0)
        x1 += b0;

    assert((an * x1) % bn == 1);
    return x1;
}

bool ECC::isOnCurve(ECC::curve curve, EllipticPoint point)
{
    if (point.X() == BigNumber() && point.Y() == BigNumber())
        return true;
    if ((point.Y() * point.Y() - point.X() * point.X() * point.X() - curve.a * point.X() - curve.b) % curve.p
        == 0)
        return true;
    else
        return false;
}

EllipticPoint ECC::negatePoint(ECC::curve curve, EllipticPoint point)
{
    if (!isOnCurve(curve, point))
        return EllipticPoint();
    EllipticPoint res(point.X(), -point.Y() % curve.p);
    assert(isOnCurve(curve, res));
    return res;
}

EllipticPoint ECC::add(ECC::curve curve, EllipticPoint a, EllipticPoint b)
{
    BigNumber m(0);
    bool aioc = isOnCurve(curve,a);
//    assert(isOnCurve(curve, a));
    assert(isOnCurve(curve, b));
    if (a.isZero())
        return b;
    if (b.isZero())
        return a;
    if (a.X() == b.X())
    {
        BigNumber z = BigNumber("2") * a.Y();
        m = (BigNumber("3") * a.X() * a.X() + curve.a) * inverseMod(z, curve.p);
    }
    else
        m = (b.Y() - a.Y()) * inverseMod(b.X() - a.X(), curve.p);
    EllipticPoint res;
    BigNumber x3 = m * m - a.X() - b.X();
    BigNumber y3 = m * (a.X() - x3) - a.Y();
    y3 = y3 % curve.p;

    x3 = x3 % curve.p;

    res.setX(x3);
    res.setY(y3);
    assert(isOnCurve(curve, res));
    return res;
}

EllipticPoint ECC::multiply(ECC::curve curve, BigNumber k, EllipticPoint point)
{
    assert(isOnCurve(curve, point));
    if (k % curve.n == 0)
        return EllipticPoint();
    if (k < 0)

        return multiply(curve, -k, negatePoint(curve, point));

    EllipticPoint res;
    EllipticPoint addend = point;
    BigNumber t;
    while (k != BigNumber("0"))
    {
        t = k & 1;
        if (t != 0)
        {
            res = add(curve, res, addend);
        }
        addend = add(curve, addend, addend);
        k = (k >> 1);
    }
    assert(isOnCurve(curve, res));
    return res;
}
