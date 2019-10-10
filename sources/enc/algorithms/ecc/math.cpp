#include "enc/algorithms/ecc/math.h"

BigNumber ECC::inverseMod(BigNumber k, BigNumber p)
{
    std::cout << "k: " << k.toByteArray(10).toStdString() << std::endl;
    std::cout << "p: " << p.toByteArray(10).toStdString() << std::endl;
    if (k < 0)
        return p - inverseMod(-k, p);
    assert(k != 0);
    BigNumber kc = k, pc = p;
    BigNumber b0 = p, t, q;
    BigNumber x0 = 0, x1 = 1;
    if (p == 1)
        return 1;
    while (kc > 1)
    {
        q = kc / pc;
        t = pc;
        pc = kc % pc;
        kc = t;
        t = x0;
        x0 = x1 - q * x0;
        x1 = t;
    }
    if (x1 < 0)
        x1 += b0;
    std::cout << "k: " << kc.toByteArray(10).toStdString() << std::endl;
    std::cout << "x1: " << x1.toByteArray(10).toStdString() << std::endl;
    std::cout << "p: " << pc.toByteArray(10).toStdString() << std::endl;
    assert((k * x1) % p == 1);
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
    assert(isOnCurve(curve, a));
    assert(isOnCurve(curve, b));
    if (a.isZero())
        return b;
    if (b.isZero())
        return a;
    //    std::cout << "aY: " << a.Y().toByteArray().toStdString() << std::endl;
    //    std::cout << "bn2 * aY: " << (BigNumber("2") * a.Y()).toByteArray().toStdString() << std::endl;
    if (a.X() == b.X())
    {
        BigNumber z = BigNumber("2") * a.Y();
        //        std::cout << "z: " << z.toByteArray().toStdString() << std::endl;
        m = (BigNumber("3") * a.X() * a.X() + curve.a) * inverseMod(z, curve.p);
    }
    else
        m = (a.Y() - b.Y()) * inverseMod(a.X() - b.X(), curve.p);
    EllipticPoint res;
    //    std::cout << "m: " << m.toByteArray().toStdString() << std::endl;
    BigNumber x3 = m * m - a.X() - b.X();
    BigNumber y3 = a.Y() + m * (x3 - a.X());
    y3 = (-y3) % curve.p;
    x3 = x3 % curve.p;
    res.setX(x3);
    res.setY(y3);
    //    std::cout << "res x: " << res.X().toByteArray().toStdString() << std::endl;
    //    std::cout << "res y: " << res.Y().toByteArray().toStdString() << std::endl;
    assert(isOnCurve(curve, res)); // ERROR
    return res;
}

EllipticPoint ECC::multiply(ECC::curve curve, BigNumber k, EllipticPoint point)
{
    BigNumber v = BigNumber::random(curve.p);

    //    std::cout << "G verification: " << isOnCurve(curve, curve.g) << std::endl;
    assert(isOnCurve(curve, point));
    //    std::exit(0);
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
            //            std::cout << "res x: " << res.X().toByteArray().toStdString() << std::endl;
            //            std::cout << "res y: " << res.Y().toByteArray().toStdString() << std::endl;
            res = add(curve, res, addend);
            //            std::cout << "res x: " << res.X().toByteArray().toStdString() << std::endl;
            //            std::cout << "res y: " << res.Y().toByteArray().toStdString() << std::endl;
        }
        addend = add(curve, addend, addend);
        k = (k >> 1);
        //        std::cout << k.toByteArray().toStdString() << std::endl;
    }
    assert(isOnCurve(curve, res));
    return res;
}
