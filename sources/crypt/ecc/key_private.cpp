#include "crypt/ecc/key_private.h"

KeyPrivate::KeyPrivate()
{
    BigNumber temp = BigNumber::random(64);
    EllipticPoints prTemp = EllipticPoints::calcPointOnCurve(temp);
    this->prkey = prTemp.getX().toByteArray();
    this->prkey.append(prTemp.getY().toByteArray());
    this->pbkey = ECC::GPoint * this->prkey;
    //    this->pbkey = EllipticPoints::calcPointOnCurve(
    //        (ECC::GPoint * (BigNumber(this->getPrivateX()))).getX());
}
KeyPrivate::KeyPrivate(const QByteArray &keyPrivate)
{
    //    int size = keyPrivate.size() / 2;
    //    QByteArray temp;
    //    temp.clear();
    //    for (int i = 0; i < size; i++)
    //        temp.append(this->prkey[i]);
    //    this->prkey = temp;
    //    this->prkey.append(EllipticPoints::calcPointOnCurve(temp).getY().toByteArray());
    //    this->pbkey = EllipticPoints::calcPointOnCurve(
    //        (ECC::GPoint * (BigNumber(this->getPrivateX()))).getX());
    this->prkey = keyPrivate;
    this->pbkey = ECC::GPoint * prkey;
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

QByteArray KeyPrivate::decrypt(const QByteArray &data)
{

    QList<QByteArray> res;
    res = Serialization::universalDesirialize(data, Serialization::DEFAULT_FIELD_SIZE);
    qDebug() << res.size();
    if (res.size() != 3)
    {
        qDebug() << "Wrong data \n Error in decrypt keyprivate.";
        return "ERROR";
    }
    QByteArray s = res.at(0);
    EllipticPoints R(res.at(1), res.at(2));

    EllipticPoints S2 = R * this->prkey;
    return S2.CryptMessage(s);
}

QByteArray KeyPrivate::sign(const QByteArray &data)
{
    //    qDebug() << "Sign data : " << data;
    //    qDebug() << "prKey" << this->prkey;
    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    BigNumber kmodp("1");
    BigNumber r("1");
    BigNumber p = BigNumber("1");
    BigNumber s("1");
    QByteArray result;
    QList<QByteArray> res;
    do
    {
        BigNumber k = BigNumber::random(30);
        EllipticPoints R = ECC::GPoint * k;
        while (!p.isPrime())
            p = BigNumber::random(6);
        r = R.getX() % p;
        kmodp = ECC::eea(k, p);
        if (kmodp == 0)
            continue;
        s = (kmodp * (hashMessage + (BigNumber(this->prkey) * r))) % p;
        if (r < 1 || (r > p - BigNumber("1")))
            continue;
        if (s < 1 || (s > p - BigNumber("1")))
            continue;
        res.clear();
        res.append(s.toByteArray());
        res.append(r.toByteArray());
        res.append(p.toByteArray());
        result = Serialization::universalSerialize(res, Serialization::DEFAULT_FIELD_SIZE);
    } while (!(verify(data, result)));
    return result;
}

bool KeyPrivate::verify(const QByteArray &data, const QByteArray &dsignBase64)
{
    QList<QByteArray> res;
    res = Serialization::universalDesirialize(dsignBase64, Serialization::DEFAULT_FIELD_SIZE);
    BigNumber s = res.at(0);
    BigNumber r = res.at(1);
    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    BigNumber p = res.at(2);
    BigNumber w = ECC::eea(s, p);
    BigNumber u1 = (hashMessage * w) % p;
    BigNumber u2 = (r * w) % p;
    EllipticPoints m;
    EllipticPoints sr;
    EllipticPoints kr = m + sr;
    EllipticPoints temp = pbkey * u2;
    EllipticPoints P = ECC::GPoint * u1 + temp;
    BigNumber px = P.getX() % p - 1;
    px.setPositive(true);
    if (px != r)
        return false;
    return true;
}

QByteArray KeyPrivate::extractPublicKey()
{
    QByteArray res;
    res.clear();
    res.append(this->pbkey.getX().toByteArray());
    res.append(this->pbkey.getY().toByteArray());
    return res;
}
QByteArray KeyPrivate::getPublicKey()
{
    QByteArray res;
    res.clear();
    res.append(this->pbkey.getX().toByteArray());
    res.append(this->pbkey.getY().toByteArray());
    return res;
}

QByteArray KeyPrivate::getPrivateX()
{
    int size = this->prkey.size() / 2;
    QByteArray temp;
    temp.clear();
    for (int i = 0; i < size; i++)
        temp.append(this->prkey[i]);
    return temp;
}

QByteArray KeyPrivate::getPrivateY()
{
    int size = this->prkey.size() / 2;
    QByteArray temp;
    temp.clear();
    for (int i = size; i < size * 2; i++)
        temp.append(this->prkey[i]);
    return temp;
}
/*Serialization::serialize(res, Serialization::DEFAULT_FIELD_SPLITTER);

   QList<QByteArray> split = Serialization::deserialize(keyPair);
   if (split.size() != 2)
   {
      qDebug() << "Input should be [prKey:pubKey]";
   }
   else
   {
       QByteArray privateKey = split[0];
       QByteArray publicKey = split[1];
       try
       {
           loadPrivateKey(privateKey);
       }
       catch (const std::exception& e)
       {
           qDebug() << "Exception while loading private key"
                    << privateKey;
           std::cout << e.what() << std::endl;
       }
       try
       {
           loadPublicKey(publicKey);
       }
       catch (const std::exception& e)
       {
           qDebug() << "Exception while loading public key"
                    << publicKey;
           std::cout << e.what() << std::endl;
       }
   }
*/
