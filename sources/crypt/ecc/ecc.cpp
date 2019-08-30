#include "crypt/ecc/ecc.h"

/*
BigNumber ECC::eea(BigNumber i, BigNumber j)
{
    BigNumber s(1), t(0), u(0), v(1);

    while (j != 0)
    {
        BigNumber q = i / j, r = i % j;
        BigNumber unew = s, vnew = t;

        s = u - (q * s);
        t = v - (q * t);
        i = j;
        j = r;
        u = unew;
        v = vnew;
    }

    return u; // m
    // return std::make_tuple(i, u, v); // d, m, n
}*/

BigNumber ECC::eea(BigNumber a, BigNumber b)
{
    BigNumber b0 = b, t, q;
    BigNumber x0 = 0, x1 = 1;

    if (b == 1)
        return 1;

    while (a > 1)
    {
        q = a / b;
        t = b, b = a % b, a = t;
        t = x0, x0 = x1 - q * x0, x1 = t;
    }

    if (x1 < 0)
        x1 = x1 + b0;
    return x1;
}

/*
BigNumber ECC::eea(BigNumber i, BigNumber j)
{


    if ((i < 0) || (j <= i))
        i = i % j;
    BigNumber c = i;
    BigNumber d = j;
    BigNumber uc = BigNumber("1");
    BigNumber vc = BigNumber("0");
    BigNumber ud = BigNumber("0");
    BigNumber vd = BigNumber("1");
    BigNumber q;

    BigNumber tempd;
    BigNumber tempc;

    BigNumber tempud;
    BigNumber tempuc;
    BigNumber tempvd;
    BigNumber tempvc;
    while (c != BigNumber("0"))
    {
        tempd = d;
        tempc = c;
        q = tempd / tempc;
        c = tempd % tempc;
        d = tempc;

        tempud = ud;
        tempuc = uc;
        tempvd = vd;
        tempvc = vc;
        uc = tempud - q * tempuc;
        vc = tempvd - q * tempvc;
        ud = tempuc;
        vd = tempvc;
    }

    if (ud > BigNumber("0"))
        return ud;
    else
        return ud + j;
}
*/

/*
BigNumber ECC::Primitives::add(BigNumber a, BigNumber b)
{
    return a + b;
}

QByteArray ECC::Primitives::sub(QByteArray a, QByteArray b)
{
    return a + b;
}

QByteArray ECC::Primitives::mult(QByteArray a, QByteArray b)
{
    return a + b;
}

QByteArray ECC::Primitives::div(QByteArray a, QByteArray b)
{
    return a + b;
}

QByteArray ECC::Primitives::exp(QByteArray a, QByteArray b)
{
    return a + b;
}

QByteArray ECC::Primitives::sqr(QByteArray a, QByteArray b)
{
    return a + b;
}
*/
