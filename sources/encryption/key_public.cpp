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

#include "encryption/key_public.h"

KeyPublic::KeyPublic(const PublicKey &public_key) {
    public_key_ = public_key;
}

KeyPublic::KeyPublic(const KeyPublic &key_public) {
    public_key_ = key_public.public_key();
}

KeyPublic::KeyPublic(const std::string &public_key) {
    public_key_ = ByteArray(public_key).toArray<crypto_sign_PUBLICKEYBYTES>();
}

std::expected<bool, Cryptography::CryptoError> KeyPublic::verify(const Bytes     &data,
                                                                 const Signature &signature) const {
    return Cryptography::verify(data, public_key_, signature);
}

std::expected<bool, Cryptography::CryptoError> KeyPublic::verify(const std::string &data,
                                                                 const Signature   &signature) const {
    return Cryptography::verify(ByteArray(data).toBytes(), public_key_, signature);
}

const PublicKey &KeyPublic::public_key() const {
    return public_key_;
}

bool KeyPublic::empty() const {
    return Utils::is_container_empty(public_key_);
}
