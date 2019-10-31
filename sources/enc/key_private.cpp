#include "enc/key_private.h"
KeyPrivate::KeyPrivate()
{
    this->prkey = BigNumber();
    this->pbkey = EllipticPoint();
}
KeyPrivate::KeyPrivate(const QByteArray &keyPrivate)
{

    QList<QByteArray> list = Serialization::universalDeserialize(keyPrivate, 3);
    this->prkey = BigNumber(list[0]);
    this->pbkey = EllipticPoint(list[1]);
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate)
{
    prkey = keyPrivate.prkey;
    pbkey = keyPrivate.pbkey;
}

KeyPrivate::~KeyPrivate()
{
}

EllipticPoint KeyPrivate::generate()
{
    try
    {
        this->prkey = BigNumber::random(64, curve.p, false);
        this->pbkey = ECC::multiply(this->curve, this->prkey, curve.g);
        QByteArray s = this->sign("test");
        this->verify("test", s);
    } catch (std::exception &)
    {
        return this->generate();
    }
    return this->pbkey;
    //    std::cout << "Key built!!!!!" << std::endl;
}

QByteArray KeyPrivate::encrypt(const QByteArray &data)
{
    QList<QByteArray> res;
    QByteArray result;
    BigNumber r;
    EllipticPoint R;
    EllipticPoint S;
    ECC::curve secpCurve;
    do
    {
        res.clear();

        r = BigNumber::random(64, curve.p, false);
        R = ECC::multiply(secpCurve, r, secpCurve.g);
        S = ECC::multiply(secpCurve, r, this->pbkey);
        res.append(blowFish_crypt().EncryptBlowFish(data, S.X().toByteArray() + S.Y().toByteArray()));
        res.append(R.X().toByteArray());
        res.append(R.Y().toByteArray());
        result = Serialization::universalSerialize(res, Serialization::DEFAULT_FIELD_SIZE);
        res.clear();
        res = Serialization::universalDeserialize(result, Serialization::DEFAULT_FIELD_SIZE);
    } while (res.size() != 3);
    return result;
}

QByteArray KeyPrivate::decrypt(const QByteArray &data)
{
    ECC::curve secpCurve;
    QList<QByteArray> res;
    res = Serialization::universalDeserialize(data, Serialization::DEFAULT_FIELD_SIZE);
    qDebug() << res.size();
    if (res.size() != 3)
    {
        qDebug() << "Wrong data \n Error in decrypt keyprivate.";
        return "ERROR";
    }
    QByteArray s = res.at(0);
    EllipticPoint R(res.at(1), res.at(2));
    EllipticPoint S2 = ECC::multiply(secpCurve, this->prkey, R);
    return blowFish_crypt().DecryptBlowFish(s, S2.X().toByteArray() + S2.Y().toByteArray());
}

QByteArray KeyPrivate::sign(const QByteArray &data)
{
    try
    {
        BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
        BigNumber r, s, k;
        EllipticPoint point;

        while ((r == 0) || (s == 0))
        {
            k = BigNumber::random(curve.n, false);
            point = ECC::multiply(curve, k, curve.g);
            r = point.X() % curve.n;
            s = ((hashMessage + r * this->prkey) * ECC::inverseMod(k, curve.n)) % curve.n;
        }

        QList<QByteArray> list;
        list.append(r.toByteArray());
        list.append(s.toByteArray());

        QByteArray dsignBase64 = Serialization::universalSerialize(list, 3);
        assert(verify(data, dsignBase64));

        return dsignBase64;
    } catch (std::exception &)
    {
        qDebug() << "Cant create sign, redone";
        return sign(data);
    }
}

bool KeyPrivate::verify(const QByteArray &data, const QByteArray &dsignBase64)
{
    BigNumber z = BigNumber(Utils::calcKeccak(data));
    QList<QByteArray> signature = Serialization::universalDeserialize(dsignBase64, 3);
    BigNumber r(signature[0]), s(signature[1]);
    BigNumber w = ECC::inverseMod(s, curve.n);
    BigNumber u1 = (z * w) % curve.n;
    BigNumber u2 = (r * w) % curve.n;
    EllipticPoint p1 = ECC::multiply(curve, u1, curve.g);
    assert(!pbkey.isZero());
    EllipticPoint p2 = ECC::multiply(curve, u2, pbkey);
    EllipticPoint point = ECC::add(curve, p1, p2);
    return r % curve.n == point.X() % curve.n;
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
    QList<QByteArray> list = { prkey.toByteArray(16), pbkey.serialize() };
    return Serialization::universalSerialize(list, 3);
}

BigNumber KeyPrivate::extractPrivateKey()
{
    return this->prkey;
}
BigNumber KeyPrivate::getPrivateKey()
{
    return extractPrivateKey();
}
