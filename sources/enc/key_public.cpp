#include "enc/key_public.h"

KeyPublic::KeyPublic(EllipticPoint pbKey)
{
    this->pbkey = pbKey;
}

KeyPublic::KeyPublic(const QJsonObject &json)
{
    auto publicKey = json["publicKey"].toObject();
    BigNumber x = publicKey["x"].toString().toLatin1();
    BigNumber y = publicKey["y"].toString().toLatin1();

    this->pbkey = EllipticPoint(x, y);
}

KeyPublic::KeyPublic(const KeyPublic &keyPublic)
{
    pbkey = keyPublic.pbkey;
}

KeyPublic::KeyPublic()
{
    pbkey = EllipticPoint();
}

KeyPublic::~KeyPublic()
{
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

        r = BigNumber::random(64, curve.p, false).nextPrime();
        R = ECC::multiply(secpCurve, r, secpCurve.g);
        S = ECC::multiply(secpCurve, r, this->pbkey);
        res.append(BlowFish::encrypt(data, S.x().toByteArray() + S.y().toByteArray()));
        res.append(R.x().toByteArray());
        res.append(R.y().toByteArray());
        result = Serialization::serialize(res, Serialization::DEFAULT_FIELD_SIZE);
        res.clear();
        res = Serialization::deserialize(result, Serialization::DEFAULT_FIELD_SIZE);
    } while (res.size() != 3);

    return result;
}

bool KeyPublic::verify(const QByteArray &data, const QByteArray &dsignBase64)
{
    BigNumber z = BigNumber(Utils::calcKeccak(data));
    QList<QByteArray> signature = Serialization::deserialize(dsignBase64, 3);
    BigNumber r(signature[0]), s(signature[1]);
    BigNumber w = ECC::inverseMod(s, curve.n);
    BigNumber u1 = (z * w) % curve.n;
    BigNumber u2 = (r * w) % curve.n;
    EllipticPoint p1 = ECC::multiply(curve, u1, curve.g);
    EllipticPoint p2 = ECC::multiply(curve, u2, pbkey);
    EllipticPoint point = ECC::add(curve, p1, p2);
    return r % curve.n == point.x() % curve.n;
}

EllipticPoint KeyPublic::getPublicKey() const
{
    return pbkey;
}

bool KeyPublic::isEmpty()
{
    return pbkey.isZero();
}
