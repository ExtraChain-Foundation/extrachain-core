/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include <QDebug>
#include <string>

#include <msgpack.hpp>

#include "extrachain_global.h"
#include "utils/exc_magic.h"
#include "encryption/encryption_tools.h"

class EXTRACHAIN_EXPORT KeyPublic {
private:
    PublicKey m_publicKey = PublicKey();

public:
    explicit KeyPublic() = default;
    explicit KeyPublic(const PublicKey &publicKey);
    explicit KeyPublic(const std::string &publicKey);
    KeyPublic(const KeyPublic &keyPublic);
    ~KeyPublic() = default;

    KeyPublic &operator=(const KeyPublic &other) = default;

    std::string encrypt(const Bytes &data, const PrivateKey &senderPrivateKey) const;

    bool verify(const Bytes &data, const Signature &signature) const;
    bool verify(const std::string &data, const Signature &signature) const;

    const PublicKey &publicKey() const;

    bool empty() const;

    MSGPACK_DEFINE(m_publicKey)
    BOOST_DESCRIBE_CLASS(KeyPublic, (), (), (), (m_publicKey))
};

MAKE_MAGICAL(KeyPublic)
