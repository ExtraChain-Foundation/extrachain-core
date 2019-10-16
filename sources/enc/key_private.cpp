#include "enc/key_private.h"
KeyPrivate::KeyPrivate()
{
    this->curve = ECC::secp256k1();
    this->prkey = BigNumber::random(curve.p);
    this->pbkey = ECC::multiply(this->curve, this->prkey, curve.g);
    std::cout << "Key built!!!!!" << std::endl;
}
KeyPrivate::KeyPrivate(const QByteArray &keyPrivate)
{
    this->curve = ECC::secp256k1();
    this->prkey = BigNumber(keyPrivate.mid(0, 64));
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
    QList<QByteArray> res;
    QByteArray result;
    BigNumber r;
    EllipticPoint R;
    EllipticPoint S;
    ECC::secp256k1 secpCurve;
    do
    {
        res.clear();

        r = BigNumber("8e3a39507dba57c317710baf41abe94ac7a8db9184748988fd7ba5b4bb26d693");
        qDebug() << "r=" << r.toByteArray();
        R = ECC::multiply(secpCurve, r, secpCurve.g);
        qDebug() << "Encrypt R.X=" << R.X().toByteArray() << " R.Y=" << R.Y().toByteArray();
        S = ECC::multiply(secpCurve, r, this->pbkey);
        res.append(blowFish_crypt().EncryptBlowFish(data, S.X().toByteArray() + S.Y().toByteArray()));
        qDebug() << "Encrypt crypt message="
                 << blowFish_crypt().EncryptBlowFish(data, S.X().toByteArray() + S.Y().toByteArray());
        res.append(R.X().toByteArray());
        qDebug() << "Encrypt X=" << S.X().toByteArray();
        res.append(R.Y().toByteArray());
        qDebug() << "Encrypt Y=" << S.Y().toByteArray();
        result = Serialization::universalSerialize(res, Serialization::DEFAULT_FIELD_SIZE);
        res.clear();
        res = Serialization::universalDesirialize(result, Serialization::DEFAULT_FIELD_SIZE);
    } while (res.size() != 3);
    return result;
    // return "";
}

QByteArray KeyPrivate::decrypt(const QByteArray &data)
{
    qDebug() << "START DECRYPTTTTTTTTTTTTTTT";
    ECC::secp256k1 secpCurve;
    QList<QByteArray> res;
    res = Serialization::universalDesirialize(data, Serialization::DEFAULT_FIELD_SIZE);
    qDebug() << res.size();
    if (res.size() != 3)
    {
        qDebug() << "Wrong data \n Error in decrypt keyprivate.";
        return "ERROR";
    }
    QByteArray s = res.at(0);
    EllipticPoint R(res.at(1), res.at(2));
    qDebug() << "Decrypt R.X=" << R.X().toByteArray() << " R.Y=" << R.Y().toByteArray();
    //    EllipticPoint S2 = R * this->prkey;
    EllipticPoint S2 = ECC::multiply(secpCurve, this->prkey, R);

    qDebug() << "Decrypt X=" << S2.X().toByteArray();
    qDebug() << "Decrypt Y=" << S2.Y().toByteArray();

    // qDebug() << "Decrypt S3 X=" << S3.X().toByteArray();
    // qDebug() << "Decrypt S3 Y=" << S3.Y().toByteArray();
    return blowFish_crypt().DecryptBlowFish(s, S2.X().toByteArray() + S2.Y().toByteArray());
    // return S2.CryptMessage(s);
    // return "";
}

QByteArray KeyPrivate::sign(const QByteArray &data)
{
    //    //    qDebug() << "Sign data : " << data;
    //    //    qDebug() << "prKey" << this->prkey;
    BigNumber hashMessage = BigNumber(Utils::calcKeccak(data));
    BigNumber r, s, k;
    EllipticPoint point;
    while ((r == 0) || (s == 0))
    {
        k = BigNumber::random(curve.n);
        point = ECC::multiply(curve, k, curve.g);
        r = point.X() % curve.n;
        s = ((hashMessage + r * this->prkey) * ECC::inverseMod(k, curve.n)) % curve.n;
    }
    QList<QByteArray> list;
    std::cout << "r: " << r.toByteArray().size() << std::endl << "s: " << s.toByteArray().size() << std::endl;
    std::cout << r.toByteArray().toStdString() << " " << s.toByteArray().toStdString() << std::endl;
    list.append(r.toByteArray());
    list.append(s.toByteArray());
    return Serialization::universalSerialize(list, 3);
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
}

bool KeyPrivate::verify(const QByteArray &data, const QByteArray &dsignBase64)
{
    BigNumber z = BigNumber(Utils::calcKeccak(data));
    QList<QByteArray> signature = Serialization::universalDesirialize(dsignBase64, 3);
    BigNumber r(signature[0]), s(signature[1]);
    BigNumber w = ECC::inverseMod(s, curve.n);
    BigNumber u1 = (z * w) % curve.n;
    BigNumber u2 = (r * w) % curve.n;
    EllipticPoint point =
        ECC::add(curve, (ECC::multiply(curve, u1, curve.g)), (ECC::multiply(curve, u2, pbkey)));
    return r % curve.n == point.X() % curve.n;
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
    return prkey.toByteArray(16) + pbkey.serialize();
}

BigNumber KeyPrivate::extractPrivateKey()
{
    return this->prkey;
}
BigNumber KeyPrivate::getPrivateKey()
{
    return extractPrivateKey();
}
