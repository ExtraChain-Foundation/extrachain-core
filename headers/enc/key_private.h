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

#ifndef KEY_PRIVATE_H
#define KEY_PRIVATE_H

#include <string>
#include "headers/utils/exc_utils.h"
#include <sodium.h>
#include <QDebug>

using namespace std;

class KeyPrivate
{
private:
    string secKey;
    string pubKey;

public:
    /**
     * @brief New keys
     */
    KeyPrivate();
    /**
     * @brief Existing keys
     * @param keyPair - [prKey:pubKey]
     */
    KeyPrivate(const QJsonObject &json);
    KeyPrivate(const KeyPrivate &keyPrivate);
    ~KeyPrivate();

public:
    void generate();

public:
    QByteArray encrypt(const QByteArray &data, const string &publicKeyReceiver);
    QByteArray decrypt(const QByteArray &data, const string &publicKeySender);
    QByteArray encryptSelf(const QByteArray &data);
    QByteArray decryptSelf(const QByteArray &data);

public:
    QByteArray sign(const QByteArray &data);
    bool verify(const QByteArray &data, const QByteArray &dsignHex);

public:
    std::string getSecKey() const;
    std::string getPubKey() const;
};

#endif // KEY_PRIVATE_H
