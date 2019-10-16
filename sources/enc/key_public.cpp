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
    //    QList<QByteArray> res;
    //    QByteArray result;
    //    BigNumber r;
    //    EllipticPoints R;
    //    EllipticPoints S;
    //    do
    //    {
    //        res.clear();
    //        r = BigNumber::random(30);
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
