#include "crypt/ecc/key_public.h"
KeyPublic::KeyPublic(QByteArray pbKey)
{

    int size = pbKey.size() / 2;
    QByteArray temp;
    temp.clear();
    for (int i = 0; i < size; i++)
        temp.append(pbKey[i]);

    this->pbkey.setX(temp);

    temp.clear();
    for (int i = size; i < pbKey.size(); i++)
        temp.append(pbKey[i]);
    this->pbkey.setY(temp);
    // this->pbkey = EllipticPoints::calcPointOnCurve(temp);
}
KeyPublic::KeyPublic(const KeyPublic &keyPrivate)
{
    pbkey = keyPrivate.pbkey;
}
QByteArray KeyPublic::encrypt(const QByteArray &data)
{
    QList<QByteArray> res;
    QByteArray result;
    BigNumber r;
    EllipticPoints R;
    EllipticPoints S;
    do
    {
        res.clear();
        r = BigNumber::random(30);
        R = ECC::GPoint * r;
        S = this->pbkey * r;
        res.append(S.CryptMessage(data));
        res.append(R.getX().toByteArray());
        res.append(R.getY().toByteArray());
        result = Serialization::universalSerialize(res, Serialization::DEFAULT_FIELD_SIZE);
        res.clear();
        res = Serialization::universalDesirialize(result, Serialization::DEFAULT_FIELD_SIZE);
    } while (res.size() != 3);
    return result;
}

bool KeyPublic::verify(const QByteArray &data, const QByteArray &dsignBase64)
{
    //    qDebug() << "Verify data : " << data;
    QList<QByteArray> res;
    res = Serialization::universalDesirialize(dsignBase64, Serialization::DEFAULT_FIELD_SIZE);
    BigNumber s = res.at(0);
    BigNumber r = res.at(1);
    // EllipticPoints Qa(res.at(2),res.at(3));

    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    BigNumber p = res.at(2);

    BigNumber w = ECC::eea(s, p);

    BigNumber u1 = (hashMessage * w) % p;

    BigNumber u2 = (r * w) % p;

    res = Serialization::universalDesirialize(dsignBase64, Serialization::DEFAULT_FIELD_SIZE);
    //    BigNumber s = res.at(0);
    //    BigNumber r = res.at(1);
    //    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    //    BigNumber p = res.at(2);
    //    BigNumber w = ECC::eea(s, p);
    //    BigNumber u1 = (hashMessage * w) % p;
    //    BigNumber u2 = (r * w) % p;
    EllipticPoints temp = pbkey * u2;
    EllipticPoints P = ECC::GPoint * u1 + temp;
    BigNumber px = P.getX() % p;
    px.setPositive(true);
    px++;
    qDebug() << px << " " << r << "1234567890";
    if (px != r)
        return false;
    return true;
}
QByteArray KeyPublic::extractPublicKey()
{
    //    QByteArray k = pbkey->extractPublicKey();
    //    QByteArray sk = sign_pbkey->extractPublicKey();
    //    if (k == sk)
    //    {
    //        return k;
    //    }
    //    else
    //    {
    //        return QByteArray();
    //    }
    //    QList<QByteArray> res;
    //    res.clear();
    //    res.append(this->pbkey.getX().toByteArray());
    //    res.append(this->pbkey.getY().toByteArray());
    //    return Serialization::serialize(res, Serialization::DEFAULT_FIELD_SPLITTER);
    return (this->pbkey.getX().toByteArray().append(this->pbkey.getY().toByteArray()));
}

// bool KeyPublic::operator ==(KeyPublic &other)
//{
//    return this->extractPublicKey() == other.extractPublicKey();
//}

// QByteArray KeyPublic::getPublicKey()
//{
//    QByteArray k = pbkey->getPublicKey();
//    QByteArray sk = sign_pbkey->getPublicKey();
//    if (k == sk)
//    {
//        return k;
//    }
//    else
//    {
//        return QByteArray();
//    }
//}
QByteArray KeyPublic::getPublicKey()
{
    return extractPublicKey();
}
