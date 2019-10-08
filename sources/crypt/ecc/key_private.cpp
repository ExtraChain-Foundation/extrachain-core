#include "crypt/ecc/key_private.h"
KeyPrivate::KeyPrivate()
{
    this->curve = ECC::secp256k1();
    BigNumber temp = BigNumber::random(64, curve.n);
    this->prkey = temp.toByteArray();
    this->pbkey = ECC::multiply(this->curve, temp, curve.g);
}
KeyPrivate::KeyPrivate(const QByteArray &keyPrivate)
{
    this->prkey = keyPrivate.mid(0, 64);
    this->pbkey = EllipticPoint(BigNumber(keyPrivate.mid(64, 64)), BigNumber(keyPrivate.mid(128, 64)));
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate)
{
    prkey = keyPrivate.prkey;
    pbkey = keyPrivate.pbkey;
}

KeyPrivate::~KeyPrivate()
{
}

QByteArray KeyPrivate::encrypt(const QByteArray &data)
{
    //    QList<QByteArray> res;
    //    QByteArray result;
    //    BigNumber r;
    //    EllipticPoints R;
    //    EllipticPoints S;
    //    do
    //    {
    //        res.clear();
    //        r = BigNumber::random(64);
    //        R = ECC::GPoint * r;
    //        S = this->pbkey * r;
    //        res.append(S.CryptMessage(data));
    //        res.append(R.getX().toByteArray());
    //        res.append(R.getY().toByteArray());
    //        result = Serialization::universalSerialize(res, Serialization::DEFAULT_FIELD_SIZE);
    //        res.clear();
    //        res = Serialization::universalDesirialize(result, Serialization::DEFAULT_FIELD_SIZE);
    //    } while (res.size() != 3);
    //    return result;
    return "";
}

QByteArray KeyPrivate::decrypt(const QByteArray &data)
{

    //    QList<QByteArray> res;
    //    res = Serialization::universalDesirialize(data, Serialization::DEFAULT_FIELD_SIZE);
    //    qDebug() << res.size();
    //    if (res.size() != 3)
    //    {
    //        qDebug() << "Wrong data \n Error in decrypt keyprivate.";
    //        return "ERROR";
    //    }
    //    QByteArray s = res.at(0);
    //    EllipticPoints R(res.at(1), res.at(2));

    //    EllipticPoints S2 = R * this->prkey;
    //    return S2.CryptMessage(s);
    return "";
}

QByteArray KeyPrivate::sign(const QByteArray &data)
{
    //    //    qDebug() << "Sign data : " << data;
    //    //    qDebug() << "prKey" << this->prkey;
    //    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    //    BigNumber kmodp("1");
    //    BigNumber r("1");
    //    BigNumber p = BigNumber("1");
    //    BigNumber s("1");
    //    QByteArray result;
    //    QList<QByteArray> res;
    //    do
    //    {
    //        BigNumber k = BigNumber::random(64);
    //        EllipticPoints R = ECC::GPoint * k;
    //        while (!p.isPrime())
    //            p = BigNumber::random(6);
    //        r = R.getX() % p;
    //        kmodp = ECC::eea(k, p);
    //        if (kmodp == 0)
    //            continue;
    //        s = (kmodp * (hashMessage + (BigNumber(this->prkey) * r))) % p;
    //        if (r < 1 || (r > p - BigNumber("1")))
    //            continue;
    //        if (s < 1 || (s > p - BigNumber("1")))
    //            continue;
    //        res.clear();
    //        res.append(s.toByteArray());
    //        res.append(r.toByteArray());
    //        res.append(p.toByteArray());
    //        result = Serialization::universalSerialize(res, Serialization::DEFAULT_FIELD_SIZE);
    //    } while (!(verify(data, result)));
    //    return result;
    return "";
}

bool KeyPrivate::verify(const QByteArray &data, const QByteArray &dsignBase64)
{
    //    QList<QByteArray> res;
    //    res = Serialization::universalDesirialize(dsignBase64, Serialization::DEFAULT_FIELD_SIZE);
    //    BigNumber s = res.at(0);
    //    BigNumber r = res.at(1);
    //    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    //    BigNumber p = res.at(2);
    //    BigNumber w = ECC::eea(s, p);
    //    BigNumber u1 = (hashMessage * w) % p;
    //    BigNumber u2 = (r * w) % p;
    //    EllipticPoints m;
    //    EllipticPoints sr;
    //    EllipticPoints kr = m + sr;
    //    EllipticPoints temp = pbkey * u2;
    //    EllipticPoints P = ECC::GPoint * u1 + temp;
    //    BigNumber px = P.getX() % p - 1;
    //    px.setPositive(true);
    //    if (px != r)
    //        return false;
    return true;
}

QByteArray KeyPrivate::extractPublicKey()
{
    return this->pbkey.serialize();
}
QByteArray KeyPrivate::getPublicKey()
{
    return extractPublicKey();
}

QByteArray KeyPrivate::serialize()
{
    return prkey + pbkey.serialize();
}

QByteArray KeyPrivate::extractPrivateKey()
{
    return this->prkey;
}
QByteArray KeyPrivate::getPrivateKey()
{
    return extractPrivateKey();
}
