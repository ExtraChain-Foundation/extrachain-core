#include "enc/key_public.h"
KeyPublic::KeyPublic(EllipticPoint pbKey)
{
    this->pbkey = pbKey;
}
KeyPublic::KeyPublic(QByteArray pbKey)
{

    this->pbkey = EllipticPoint(pbKey);
}
KeyPublic::KeyPublic(const KeyPublic &keyPublic)
{
    pbkey = keyPublic.pbkey;
}

KeyPublic::KeyPublic()
{
    pbkey = EllipticPoint();
}
QByteArray KeyPublic::encrypt(const QByteArray &data)
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

bool KeyPublic::verify(const QByteArray &data, const QByteArray &dsignBase64)
{
    BigNumber z = BigNumber(Utils::calcKeccak(data));
    QList<QByteArray> signature = Serialization::universalDeserialize(dsignBase64, 3);
    BigNumber r(signature[0]), s(signature[1]);
    BigNumber w = ECC::inverseMod(s, curve.n);
    BigNumber u1 = (z * w) % curve.n;
    BigNumber u2 = (r * w) % curve.n;
    EllipticPoint p1 = ECC::multiply(curve, u1, curve.g);
    EllipticPoint p2 = ECC::multiply(curve, u2, pbkey);
    EllipticPoint point = ECC::add(curve, p1, p2);
    return r % curve.n == point.X() % curve.n;
}
QByteArray KeyPublic::extractPublicKey()
{
    return this->pbkey.serialize();
}

QByteArray KeyPublic::getPublicKey()
{
    return extractPublicKey();
}

QByteArray KeyPublic::serialize()
{
    return pbkey.serialize();
}

bool KeyPublic::isEmpty()
{
    return pbkey.isZero();
}
