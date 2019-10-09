#include "headers/crypt/ecc/math.h"

BigNumber ECC::inverseMod(BigNumber k, BigNumber p)
{
    std::cout << "k: " << k.toByteArray().toStdString() << std::endl;
    std::cout << "p: " << p.toByteArray().toStdString() << std::endl;
    assert(k != 0);
    if (k < 0)
    {
        //        k.setPositive(false);
        return p - inverseMod(-k, p);
    }
    // Extended Euclidean algorithm
    mpz_class s = 0;
    mpz_class old_s = 1;
    mpz_class t = 1;
    mpz_class old_t = 0;
    mpz_class r = p.data();

    mpz_class old_r = k.data();
    mpz_class quotient = 0;
    std::cout << "quotient1: " << quotient.get_str(16) << std::endl;
    int i = 0;
    while (r != 0)
    {
        i++;
        mpz_class temp = 0;
        quotient = old_r / r;
        temp = r;
        r = old_r - quotient * r;
        old_r = temp;

        temp = s;
        s = old_s - quotient * s;
        old_s = temp;

        temp = t;
        t = old_t - quotient * t;
        old_t = temp;
        std::cout << "r: " << r.get_str(16) << std::endl;
        std::cout << "old_r: " << old_r.get_str(16) << std::endl;
        std::cout << "s: " << s.get_str(16) << std::endl;
        std::cout << "old_s: " << old_s.get_str(16) << std::endl;
        std::cout << "t: " << t.get_str(16) << std::endl;
        std::cout << "old_t: " << old_t.get_str(16) << std::endl;
        if (i > 50)
            std::exit(0);
    }
    mpz_class gcd = old_r;
    mpz_class x = old_s; // Error
    mpz_class y = old_t; // Error

    std::cout << "kxp: " << mpz_class((k.data() * x) % p.data()).get_str(16) << std::endl; // Error
    assert(gcd == 1);
    assert((k.data() * x) % p.data() == 1); // Error

    return BigNumber(x % p.data());
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
    std::cout << "aY: " << a.Y().toByteArray().toStdString() << std::endl;
    std::cout << "bn2 * aY: " << (BigNumber("2") * a.Y()).toByteArray().toStdString() << std::endl;
    if (a.X() == b.X())
    {
        BigNumber z = BigNumber("2") * a.Y();
        std::cout << "z: " << z.toByteArray().toStdString() << std::endl;
        m = (BigNumber("3") * a.X() * a.X() + curve.a) * inverseMod(z, curve.p);
    }
    else
        m = (a.Y() - b.Y()) * inverseMod(a.X() - b.X(), curve.p);
    EllipticPoint res;
    std::cout << "m: " << m.toByteArray().toStdString() << std::endl;
    BigNumber x3 = m * m - a.X() - b.X();
    BigNumber y3 = a.Y() + m * (x3 - a.X());
    y3 = (-y3) % curve.p;
    x3 = x3 % curve.p;
    res.setX(x3);
    res.setY(y3);
    std::cout << "res x: " << res.X().toByteArray().toStdString() << std::endl;
    std::cout << "res y: " << res.Y().toByteArray().toStdString() << std::endl;
    assert(isOnCurve(curve, res)); // ERROR
    return res;
}

EllipticPoint ECC::multiply(ECC::curve curve, BigNumber k, EllipticPoint point)
{
    BigNumber v = BigNumber::random(curve.p);

    std::cout << "G verification: " << isOnCurve(curve, curve.g) << std::endl;
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
            std::cout << "res x: " << res.X().toByteArray().toStdString() << std::endl;
            std::cout << "res y: " << res.Y().toByteArray().toStdString() << std::endl;
            res = add(curve, res, addend);
            std::cout << "res x: " << res.X().toByteArray().toStdString() << std::endl;
            std::cout << "res y: " << res.Y().toByteArray().toStdString() << std::endl;
        }
        addend = add(curve, addend, addend);
        k = (k >> 1);
        std::cout << k.toByteArray().toStdString() << std::endl;
    }
    assert(isOnCurve(curve, res));
    return res;
}
