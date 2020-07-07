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

#ifndef BLOWFISH_CRYPT_H
#define BLOWFISH_CRYPT_H

#include <cstdint>
#include <cstring>
#include <QByteArray>

class BlowFish
{
public:
    static QByteArray encrypt(const QByteArray &message, const QByteArray &key);
    static QByteArray decrypt(const QByteArray &message, const QByteArray &key);

private:
    BlowFish();
    static void SetKey(const char *key, size_t byte_length, uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static void EncryptBlock(uint32_t *left, uint32_t *right, uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static void DecryptBlock(uint32_t *left, uint32_t *right, uint32_t (&pary)[18], uint32_t (&sbox)[4][256]);
    static QByteArray Encrypt(const QByteArray &src, const QByteArray &key, uint32_t (&pary)[18],
                              uint32_t (&sbox)[4][256]);
    static QByteArray Decrypt(const QByteArray &src, const QByteArray &key, uint32_t (&pary)[18],
                              uint32_t (&sbox)[4][256]);
    static uint32_t Feistel(uint32_t value, uint32_t (&sbox)[4][256]);
};

#endif // BLOWFISH_CRYPT_H
