#include "crypt/ecc/key_public.h"
KeyPublic::KeyPublic(EllipticPoint pbKey)
{
    this->pbkey = pbKey;
}
KeyPublic::KeyPublic(QByteArray pbKey)
{
    this->pbkey = EllipticPoint(BigNumber(pbKey.mid(0, 64)), BigNumber(pbKey.mid(64, 64)));
}
KeyPublic::KeyPublic(const KeyPublic &keyPublic)
{
    pbkey = keyPublic.pbkey;
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
    //    qDebug() << "Verify data : " << data;
    //    QList<QByteArray> res;
    //    res = Serialization::universalDesirialize(dsignBase64, Serialization::DEFAULT_FIELD_SIZE);
    //    BigNumber s = res.at(0);
    //    BigNumber r = res.at(1);
    //    // EllipticPoints Qa(res.at(2),res.at(3));

    //    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    //    BigNumber p = res.at(2);

    //    BigNumber w = ECC::eea(s, p);

    //    BigNumber u1 = (hashMessage * w) % p;

    //    BigNumber u2 = (r * w) % p;

    //    res = Serialization::universalDesirialize(dsignBase64, Serialization::DEFAULT_FIELD_SIZE);
    //    //    BigNumber s = res.at(0);
    //    //    BigNumber r = res.at(1);
    //    //    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    //    //    BigNumber p = res.at(2);
    //    //    BigNumber w = ECC::eea(s, p);
    //    //    BigNumber u1 = (hashMessage * w) % p;
    //    //    BigNumber u2 = (r * w) % p;
    //    EllipticPoints temp = pbkey * u2;
    //    EllipticPoints P = ECC::GPoint * u1 + temp;
    //    BigNumber px = P.getX() % p;
    //    px.setPositive(true);
    //    px++;

    //    if (px != r)
    //        return false;

    return true;
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
