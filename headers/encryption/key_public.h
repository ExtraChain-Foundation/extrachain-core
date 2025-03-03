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

#include <string>

#include <boost/core/demangle.hpp>
#include <msgpack.hpp>

#include "extrachain_global.h"
#include "encryption/encryption_tools.h"

class EXTRACHAIN_EXPORT KeyPublic {
private:
    PublicKey public_key_ = PublicKey();

public:
    explicit KeyPublic() = default;
    explicit KeyPublic(const PublicKey &public_key);
    explicit KeyPublic(const std::string &public_key);
    KeyPublic(const KeyPublic &keyPublic);
    ~KeyPublic() = default;

    KeyPublic &operator=(const KeyPublic &other) = default;

    std::expected<bool, Cryptography::CryptoError> verify(const Bytes &data, const Signature &signature) const;
    // [[deprecated("Use verify version with Bytes")]]
    std::expected<bool, Cryptography::CryptoError> verify(const std::string &data,
                                                          const Signature   &signature) const;

    const PublicKey &public_key() const;

    bool empty() const;

    MSGPACK_DEFINE(public_key_)
    BOOST_DESCRIBE_CLASS(KeyPublic, (), (), (), (public_key_))
};
