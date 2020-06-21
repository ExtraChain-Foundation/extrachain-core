#include "enc/algorithms/ecc/eccmath.h"

BigNumber ECC::inverseMod(BigNumber a, BigNumber b)
{
    mpz_class res;
    mpz_invert(res.get_mpz_t(), a.data().get_mpz_t(), b.data().get_mpz_t());
    return res;
}

bool ECC::isOnCurve(ECC::curve curve, EllipticPoint point)
{
    if (point.x() == BigNumber() && point.y() == BigNumber())
        return true;
    if ((point.y() * point.y() - point.x() * point.x() * point.x() - curve.a * point.x() - curve.b) % curve.p
        == 0)
        return true;
    else
        return false;
}

EllipticPoint ECC::negatePoint(ECC::curve curve, EllipticPoint point)
{
    if (!isOnCurve(curve, point))
        return EllipticPoint();
    EllipticPoint res(point.x(), -point.y() % curve.p);
    assert(isOnCurve(curve, res));
    return res;
}

EllipticPoint ECC::add(ECC::curve curve, EllipticPoint a, EllipticPoint b)
{
    BigNumber m(0);
    bool aioc = isOnCurve(curve, a);
    //    assert(isOnCurve(curve, a));
    assert(isOnCurve(curve, b));
    if (a.isZero())
        return b;
    if (b.isZero())
        return a;
    if (a.x() == b.x())
    {
        BigNumber z = BigNumber("2") * a.y();
        m = (BigNumber("3") * a.x() * a.x() + curve.a) * inverseMod(z, curve.p);
    }
    else
        m = (b.y() - a.y()) * inverseMod(b.x() - a.x(), curve.p);
    EllipticPoint res;
    BigNumber x3 = m * m - a.x() - b.x();
    BigNumber y3 = m * (a.x() - x3) - a.y();
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
