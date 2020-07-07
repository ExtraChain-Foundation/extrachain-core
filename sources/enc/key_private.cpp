/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "enc/key_private.h"

KeyPrivate::KeyPrivate()
{
    this->prkey = BigNumber();
    this->pbkey = EllipticPoint();
}

KeyPrivate::KeyPrivate(const QJsonObject &json)
{
    BigNumber privateKey = json["privateKey"].toString().toLatin1();
    auto publicKey = json["publicKey"].toObject();
    BigNumber x = publicKey["x"].toString().toLatin1();
    BigNumber y = publicKey["y"].toString().toLatin1();

    this->prkey = privateKey;
    this->pbkey = EllipticPoint(x, y);
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
        this->prkey = BigNumber::random(64, curve.p, false).nextPrime();
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

QByteArray KeyPrivate::decrypt(const QByteArray &data)
{
    ECC::curve secpCurve;
    QList<QByteArray> res;
    res = Serialization::deserialize(data, Serialization::DEFAULT_FIELD_SIZE);
    // qDebug() << res.size();
    if (res.size() != 3)
    {
        qDebug() << "Wrong data \n Error in decrypt keyprivate.";
        return "ERROR";
    }
    QByteArray s = res.at(0);
    EllipticPoint R(res.at(1), res.at(2));
    EllipticPoint S2 = ECC::multiply(secpCurve, this->prkey, R);
    return BlowFish::decrypt(s, S2.x().toByteArray() + S2.y().toByteArray());
}

QByteArray KeyPrivate::encryptSymmetric(const QByteArray &data)
{
    return BlowFish::encrypt(data, this->prkey.toByteArray());
}

QByteArray KeyPrivate::decryptSymmetric(const QByteArray &data)
{
    return BlowFish::decrypt(data, this->prkey.toByteArray());
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
            k = BigNumber::random(curve.n, false).nextPrime();
            point = ECC::multiply(curve, k, curve.g);
            r = point.x() % curve.n;
            s = ((hashMessage + r * this->prkey) * ECC::inverseMod(k, curve.n)) % curve.n;
        }

        QList<QByteArray> list;
        list.append(r.toByteArray());
        list.append(s.toByteArray());

        QByteArray dsignBase64 = Serialization::serialize(list, 3);
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
    QList<QByteArray> signature = Serialization::deserialize(dsignBase64, 3);
    BigNumber r(signature[0]), s(signature[1]);
    BigNumber w = ECC::inverseMod(s, curve.n);
    BigNumber u1 = (z * w) % curve.n;
    BigNumber u2 = (r * w) % curve.n;
    EllipticPoint p1 = ECC::multiply(curve, u1, curve.g);
    assert(!pbkey.isZero());
    EllipticPoint p2 = ECC::multiply(curve, u2, pbkey);
    EllipticPoint point = ECC::add(curve, p1, p2);
    return r % curve.n == point.x() % curve.n;
}

BigNumber KeyPrivate::getPrivateKey() const
{
    return this->prkey;
}

EllipticPoint KeyPrivate::getPublicKey() const
{
    return pbkey;
}
