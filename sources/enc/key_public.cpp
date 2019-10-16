#include "enc/key_public.h"
KeyPublic::KeyPublic(EllipticPoint pbKey)
{
    this->pbkey = pbKey;
}
KeyPublic::KeyPublic(QByteArray pbKey)
{
    this->pbkey = EllipticPoint(BigNumber(pbKey.mid(0, 64)), BigNumber(pbKey.mid(64, 65)));
}
KeyPublic::KeyPublic(const KeyPublic &keyPublic)
{
    pbkey = keyPublic.pbkey;
}
QByteArray KeyPublic::encrypt(const QByteArray &data)
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
        qDebug() << "R===========" << r.toByteArray();
        // r = BigNumber("979b09154d4109d3b41f773b4d46f59fcf89a4a013b61bbe2d6229575f32f09e");

        R = ECC::multiply(secpCurve, r, secpCurve.g);
        qDebug() << "pb key =" << this->pbkey.X().toByteArray() + this->pbkey.Y().toByteArray();
        //  S = this->pbkey * r;
        S = ECC::multiply(secpCurve, r, this->pbkey);

        //        res.append(S.CryptMessage(data));
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
