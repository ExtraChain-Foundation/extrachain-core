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

#include "chat/secret_session.h"

#include <sodium.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string_view>

namespace SecretChat {

namespace {

constexpr std::uint8_t WireVersion = 1;

constexpr std::size_t HeaderPlainSize = 32 + 4 + 4 + 8;
constexpr std::size_t NonceSize       = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
constexpr std::size_t TagSize         = crypto_aead_xchacha20poly1305_ietf_ABYTES;
constexpr std::size_t HeaderWireSize  = NonceSize + HeaderPlainSize + TagSize;
constexpr std::size_t MinWireSize     = 1 + HeaderWireSize + TagSize;

constexpr std::array<std::size_t, 5> PadBuckets { 256, 512, 1024, 2048, 4096 };

constexpr std::string_view HeaderAd   = "sc/hdr/v1";
constexpr std::string_view SpkLabel   = "sc/spk/v1";
constexpr std::string_view KdfRkInfo  = "SecretChat/RK/v1";
constexpr std::string_view X3dhInfo   = "SecretChat/X3DH/v1";
constexpr std::string_view MailboxTag = "sc/mbox/v1";
constexpr std::string_view InitialTag = "sc/init/v1";
constexpr std::string_view NonceTag   = "sc/nonce/v1";
constexpr std::string_view SafetyTag  = "sc/safety/v1";

void ensure_sodium() {
    static std::once_flag flag;
    std::call_once(flag, [] { (void)sodium_init(); });
}

template <typename T>
void wipe(T &value) {
    sodium_memzero(value.data(), value.size());
}

void put_u32(Bytes &out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put_u64(Bytes &out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put_bytes(Bytes &out, std::span<const std::uint8_t> data) {
    out.insert(out.end(), data.begin(), data.end());
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    bool take(std::uint8_t *dst, std::size_t n) {
        if (data_.size() - pos_ < n)
            return false;
        std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n;
        return true;
    }
    template <std::size_t N>
    bool take(std::array<std::uint8_t, N> &dst) {
        return take(dst.data(), N);
    }
    bool u8(std::uint8_t &v) { return take(&v, 1); }
    bool u32(std::uint32_t &v) {
        std::array<std::uint8_t, 4> b {};
        if (!take(b))
            return false;
        v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(b[i]) << (8 * i);
        return true;
    }
    bool u64(std::uint64_t &v) {
        std::array<std::uint8_t, 8> b {};
        if (!take(b))
            return false;
        v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(b[i]) << (8 * i);
        return true;
    }
    std::size_t remaining() const { return data_.size() - pos_; }
    std::span<const std::uint8_t> rest() const { return data_.subspan(pos_); }

private:
    std::span<const std::uint8_t> data_;
    std::size_t                   pos_ = 0;
};

std::expected<Key32, Error> dh(const Key32 &secret, const Key32 &public_key) {
    Key32 out {};
    if (crypto_scalarmult(out.data(), secret.data(), public_key.data()) != 0)
        return std::unexpected(Error::CryptoFailure);
    return out;
}

void new_keypair(Key32 &pk, Key32 &sk) {
    randombytes_buf(sk.data(), sk.size());
    crypto_scalarmult_base(pk.data(), sk.data());
}

std::expected<Key32, Error> x25519_public(const Key32 &identity) {
    Key32 out {};
    if (crypto_sign_ed25519_pk_to_curve25519(out.data(), identity.data()) != 0)
        return std::unexpected(Error::InvalidBundle);
    return out;
}

std::expected<Key32, Error> x25519_secret(const SignKey &identity) {
    Key32 out {};
    if (crypto_sign_ed25519_sk_to_curve25519(out.data(), identity.data()) != 0)
        return std::unexpected(Error::CryptoFailure);
    return out;
}

// Next chain key and the key of one message, as in the Double Ratchet specification
void kdf_ck(Key32 &ck, Key32 &mk) {
    const std::uint8_t one = 0x01;
    const std::uint8_t two = 0x02;
    crypto_auth_hmacsha256(mk.data(), &one, 1, ck.data());
    Key32 next {};
    crypto_auth_hmacsha256(next.data(), &two, 1, ck.data());
    ck = next;
    wipe(next);
}

struct RootStep {
    Key32 rk {};
    Key32 ck {};
    Key32 nhk {};
};

std::expected<RootStep, Error> kdf_rk(const Key32 &rk, const Key32 &dh_out) {
    std::array<std::uint8_t, crypto_kdf_hkdf_sha512_KEYBYTES> prk {};
    if (crypto_kdf_hkdf_sha512_extract(prk.data(), rk.data(), rk.size(), dh_out.data(), dh_out.size()) != 0)
        return std::unexpected(Error::CryptoFailure);
    std::array<std::uint8_t, 96> okm {};
    if (crypto_kdf_hkdf_sha512_expand(okm.data(),
                                      okm.size(),
                                      KdfRkInfo.data(),
                                      KdfRkInfo.size(),
                                      prk.data())
        != 0) {
        wipe(prk);
        return std::unexpected(Error::CryptoFailure);
    }
    RootStep step;
    std::memcpy(step.rk.data(), okm.data(), 32);
    std::memcpy(step.ck.data(), okm.data() + 32, 32);
    std::memcpy(step.nhk.data(), okm.data() + 64, 32);
    wipe(prk);
    wipe(okm);
    return step;
}

struct Agreement {
    Key32 root {};
    Key32 hka {};
    Key32 nhkb {};
};

std::expected<Agreement, Error> kdf_x3dh(const Key32 &dh1, const Key32 &dh2, const Key32 &dh3) {
    Bytes ikm(32, 0xFF);
    put_bytes(ikm, dh1);
    put_bytes(ikm, dh2);
    put_bytes(ikm, dh3);
    const std::array<std::uint8_t, 32> salt {};
    std::array<std::uint8_t, crypto_kdf_hkdf_sha512_KEYBYTES> prk {};
    if (crypto_kdf_hkdf_sha512_extract(prk.data(), salt.data(), salt.size(), ikm.data(), ikm.size()) != 0) {
        sodium_memzero(ikm.data(), ikm.size());
        return std::unexpected(Error::CryptoFailure);
    }
    sodium_memzero(ikm.data(), ikm.size());
    std::array<std::uint8_t, 96> okm {};
    if (crypto_kdf_hkdf_sha512_expand(okm.data(), okm.size(), X3dhInfo.data(), X3dhInfo.size(), prk.data()) != 0) {
        wipe(prk);
        return std::unexpected(Error::CryptoFailure);
    }
    Agreement out;
    std::memcpy(out.root.data(), okm.data(), 32);
    std::memcpy(out.hka.data(), okm.data() + 32, 32);
    std::memcpy(out.nhkb.data(), okm.data() + 64, 32);
    wipe(prk);
    wipe(okm);
    return out;
}

Bytes aead_seal(std::span<const std::uint8_t> plaintext,
                std::span<const std::uint8_t> ad,
                const std::uint8_t           *nonce,
                const Key32                  &key) {
    Bytes             out(plaintext.size() + TagSize);
    unsigned long long len = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(out.data(),
                                               &len,
                                               plaintext.data(),
                                               plaintext.size(),
                                               ad.data(),
                                               ad.size(),
                                               nullptr,
                                               nonce,
                                               key.data());
    out.resize(len);
    return out;
}

std::optional<Bytes> aead_open(std::span<const std::uint8_t> ciphertext,
                               std::span<const std::uint8_t> ad,
                               const std::uint8_t           *nonce,
                               const Key32                  &key) {
    if (ciphertext.size() < TagSize)
        return std::nullopt;
    Bytes             out(ciphertext.size() - TagSize);
    unsigned long long len = 0;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(out.data(),
                                                   &len,
                                                   nullptr,
                                                   ciphertext.data(),
                                                   ciphertext.size(),
                                                   ad.data(),
                                                   ad.size(),
                                                   nonce,
                                                   key.data())
        != 0)
        return std::nullopt;
    out.resize(len);
    return out;
}

// A message key is used once, so a nonce derived from it never repeats under that key
std::array<std::uint8_t, NonceSize> body_nonce(const Key32 &mk) {
    std::array<std::uint8_t, crypto_auth_hmacsha256_BYTES> mac {};
    crypto_auth_hmacsha256(mac.data(),
                           reinterpret_cast<const std::uint8_t *>(NonceTag.data()),
                           NonceTag.size(),
                           mk.data());
    std::array<std::uint8_t, NonceSize> nonce {};
    std::memcpy(nonce.data(), mac.data(), NonceSize);
    return nonce;
}

std::optional<Bytes> pad(std::span<const std::uint8_t> plaintext) {
    if (plaintext.size() > MaxPlaintextSize)
        return std::nullopt;
    std::size_t target = PadBuckets.back();
    for (const auto bucket : PadBuckets) {
        if (plaintext.size() + 1 <= bucket) {
            target = bucket;
            break;
        }
    }
    Bytes out(plaintext.begin(), plaintext.end());
    out.push_back(0x80);
    out.resize(target, 0x00);
    return out;
}

std::optional<Bytes> unpad(Bytes padded) {
    std::size_t end = padded.size();
    while (end > 0 && padded[end - 1] == 0x00)
        --end;
    if (end == 0 || padded[end - 1] != 0x80)
        return std::nullopt;
    padded.resize(end - 1);
    return padded;
}

MailboxId mailbox_id(const Key32 &header_key, std::uint64_t index) {
    Bytes msg(MailboxTag.begin(), MailboxTag.end());
    put_u64(msg, index);
    std::array<std::uint8_t, crypto_auth_hmacsha256_BYTES> mac {};
    crypto_auth_hmacsha256(mac.data(), msg.data(), msg.size(), header_key.data());
    MailboxId id {};
    std::memcpy(id.data(), mac.data(), id.size());
    return id;
}

struct Header {
    Key32         dh {};
    std::uint32_t pn = 0;
    std::uint32_t n  = 0;
    std::uint64_t t  = 0;
};

Bytes seal_header(const Key32 &hk, const Header &h) {
    Bytes plain;
    plain.reserve(HeaderPlainSize);
    put_bytes(plain, h.dh);
    put_u32(plain, h.pn);
    put_u32(plain, h.n);
    put_u64(plain, h.t);
    std::array<std::uint8_t, NonceSize> nonce {};
    randombytes_buf(nonce.data(), nonce.size());
    const auto ad = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(HeaderAd.data()),
                                                  HeaderAd.size());
    Bytes out(nonce.begin(), nonce.end());
    put_bytes(out, aead_seal(plain, ad, nonce.data(), hk));
    sodium_memzero(plain.data(), plain.size());
    return out;
}

std::optional<Header> open_header(const Key32 &hk, std::span<const std::uint8_t> sealed) {
    if (sealed.size() != HeaderWireSize)
        return std::nullopt;
    const auto ad = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(HeaderAd.data()),
                                                  HeaderAd.size());
    auto plain = aead_open(sealed.subspan(NonceSize), ad, sealed.data(), hk);
    if (!plain.has_value() || plain->size() != HeaderPlainSize)
        return std::nullopt;
    Reader reader(*plain);
    Header h;
    reader.take(h.dh);
    reader.u32(h.pn);
    reader.u32(h.n);
    reader.u64(h.t);
    sodium_memzero(plain->data(), plain->size());
    return h;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Identity and prekeys

std::expected<IdentityKeys, Error> IdentityKeys::from_seed(const std::array<std::uint8_t, 32> &seed) {
    ensure_sodium();
    IdentityKeys keys;
    if (crypto_sign_seed_keypair(keys.public_key.data(), keys.secret_key.data(), seed.data()) != 0)
        return std::unexpected(Error::CryptoFailure);
    return keys;
}

namespace {
Bytes prekey_signed_message(const Key32 &signed_prekey, std::uint32_t id) {
    Bytes msg(SpkLabel.begin(), SpkLabel.end());
    put_bytes(msg, signed_prekey);
    put_u32(msg, id);
    return msg;
}
} // namespace

bool PreKeyBundle::verify() const {
    ensure_sodium();
    const Bytes msg = prekey_signed_message(signed_prekey, signed_prekey_id);
    return crypto_sign_verify_detached(signature.data(), msg.data(), msg.size(), identity_key.data()) == 0;
}

Bytes PreKeyBundle::serialize() const {
    Bytes out;
    out.push_back(WireVersion);
    put_bytes(out, identity_key);
    put_bytes(out, signed_prekey);
    put_u32(out, signed_prekey_id);
    put_bytes(out, signature);
    return out;
}

std::expected<PreKeyBundle, Error> PreKeyBundle::parse(std::span<const std::uint8_t> data) {
    Reader        reader(data);
    std::uint8_t  version = 0;
    PreKeyBundle  bundle;
    if (!reader.u8(version) || version != WireVersion || !reader.take(bundle.identity_key)
        || !reader.take(bundle.signed_prekey) || !reader.u32(bundle.signed_prekey_id)
        || !reader.take(bundle.signature) || reader.remaining() != 0)
        return std::unexpected(Error::InvalidBundle);
    return bundle;
}

std::expected<SignedPreKey, Error> SignedPreKey::generate(const IdentityKeys &identity, std::uint32_t id) {
    ensure_sodium();
    SignedPreKey prekey;
    prekey.id = id;
    new_keypair(prekey.public_key, prekey.secret_key);
    prekey.bundle.identity_key     = identity.public_key;
    prekey.bundle.signed_prekey    = prekey.public_key;
    prekey.bundle.signed_prekey_id = id;
    const Bytes msg                = prekey_signed_message(prekey.public_key, id);
    if (crypto_sign_detached(prekey.bundle.signature.data(), nullptr, msg.data(), msg.size(), identity.secret_key.data())
        != 0)
        return std::unexpected(Error::CryptoFailure);
    return prekey;
}

Bytes custom_payload(std::span<const std::uint8_t> envelope) {
    Bytes out;
    const std::size_t n = envelope.size();
    if (n < 0x100) {
        out.push_back(0xc4);
        out.push_back(static_cast<std::uint8_t>(n));
    } else if (n < 0x10000) {
        out.push_back(0xc5);
        out.push_back(static_cast<std::uint8_t>(n >> 8));
        out.push_back(static_cast<std::uint8_t>(n));
    } else {
        out.push_back(0xc6);
        for (int shift = 24; shift >= 0; shift -= 8)
            out.push_back(static_cast<std::uint8_t>(n >> shift));
    }
    put_bytes(out, envelope);
    return out;
}

std::expected<Bytes, Error> open_custom_payload(std::span<const std::uint8_t> data) {
    if (data.empty())
        return std::unexpected(Error::BadFormat);
    std::size_t header = 0;
    std::size_t length = 0;
    switch (data[0]) {
    case 0xc4:
        header = 2;
        if (data.size() < header)
            return std::unexpected(Error::BadFormat);
        length = data[1];
        break;
    case 0xc5:
        header = 3;
        if (data.size() < header)
            return std::unexpected(Error::BadFormat);
        length = (static_cast<std::size_t>(data[1]) << 8) | data[2];
        break;
    case 0xc6:
        header = 5;
        if (data.size() < header)
            return std::unexpected(Error::BadFormat);
        for (int i = 1; i <= 4; ++i)
            length = (length << 8) | data[i];
        break;
    default:
        return std::unexpected(Error::BadFormat);
    }
    if (data.size() - header != length)
        return std::unexpected(Error::BadFormat);
    return Bytes(data.begin() + static_cast<std::ptrdiff_t>(header), data.end());
}

MailboxId initial_mailbox_id(const Key32 &signed_prekey) {
    ensure_sodium();
    std::array<std::uint8_t, crypto_generichash_BYTES> digest {};
    crypto_generichash(digest.data(),
                       digest.size(),
                       signed_prekey.data(),
                       signed_prekey.size(),
                       reinterpret_cast<const std::uint8_t *>(InitialTag.data()),
                       InitialTag.size());
    MailboxId id {};
    std::memcpy(id.data(), digest.data(), id.size());
    return id;
}

std::string safety_number(const Key32 &identity_a, const Key32 &identity_b) {
    ensure_sodium();
    const bool a_first = std::lexicographical_compare(identity_a.begin(), identity_a.end(), identity_b.begin(), identity_b.end());
    const Key32 &first  = a_first ? identity_a : identity_b;
    const Key32 &second = a_first ? identity_b : identity_a;
    Bytes input(SafetyTag.begin(), SafetyTag.end());
    put_bytes(input, first);
    put_bytes(input, second);
    std::array<std::uint8_t, 64> digest {};
    crypto_generichash(digest.data(), digest.size(), input.data(), input.size(), nullptr, 0);
    std::string out;
    for (int group = 0; group < 12; ++group) {
        std::uint64_t value = 0;
        for (int i = 0; i < 5; ++i)
            value = (value << 8) | digest[group * 5 + i];
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%05u", static_cast<unsigned>(value % 100000));
        if (!out.empty())
            out.push_back(' ');
        out += buf;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Session state and the ratchet

struct SessionState {
    std::array<std::uint8_t, 64> ad {};
    Key32                        self_identity {};
    Key32                        peer_identity {};

    Key32 dhs_pk {};
    Key32 dhs_sk {};
    bool  has_dhr = false;
    Key32 dhr {};

    Key32 rk {};
    bool  has_cks = false;
    Key32 cks {};
    bool  has_ckr = false;
    Key32 ckr {};

    std::uint32_t ns = 0;
    std::uint32_t nr = 0;
    std::uint32_t pn = 0;
    std::uint64_t ts = 0;
    std::uint64_t tr = 0;

    bool  has_hks = false;
    Key32 hks {};
    bool  has_hkr = false;
    Key32 hkr {};
    Key32 nhks {};
    Key32 nhkr {};

    struct Skipped {
        Key32         hk {};
        std::uint32_t n = 0;
        Key32         mk {};
    };
    std::vector<Skipped> skipped;

    SessionState()                                = default;
    SessionState(const SessionState &)            = default;
    SessionState &operator=(const SessionState &) = default;
    ~SessionState() {
        wipe(dhs_sk);
        wipe(rk);
        wipe(cks);
        wipe(ckr);
        wipe(hks);
        wipe(hkr);
        wipe(nhks);
        wipe(nhkr);
        for (auto &entry : skipped) {
            wipe(entry.hk);
            wipe(entry.mk);
        }
    }
};

namespace {

std::expected<void, Error> dh_ratchet(SessionState &s, const Header &h) {
    s.pn      = s.ns;
    s.ns      = 0;
    s.nr      = 0;
    s.hks     = s.nhks;
    s.has_hks = true;
    s.hkr     = s.nhkr;
    s.has_hkr = true;
    s.dhr     = h.dh;
    s.has_dhr = true;

    auto first = dh(s.dhs_sk, s.dhr);
    if (!first.has_value())
        return std::unexpected(first.error());
    auto recv = kdf_rk(s.rk, *first);
    wipe(*first);
    if (!recv.has_value())
        return std::unexpected(recv.error());
    s.rk      = recv->rk;
    s.ckr     = recv->ck;
    s.has_ckr = true;
    s.nhkr    = recv->nhk;

    wipe(s.dhs_sk);
    new_keypair(s.dhs_pk, s.dhs_sk);
    auto second = dh(s.dhs_sk, s.dhr);
    if (!second.has_value())
        return std::unexpected(second.error());
    auto send = kdf_rk(s.rk, *second);
    wipe(*second);
    if (!send.has_value())
        return std::unexpected(send.error());
    s.rk      = send->rk;
    s.cks     = send->ck;
    s.has_cks = true;
    s.nhks    = send->nhk;
    return {};
}

std::expected<void, Error> skip_message_keys(SessionState &s, std::uint32_t until) {
    if (static_cast<std::uint64_t>(s.nr) + MaxSkip < until)
        return std::unexpected(Error::TooManySkipped);
    if (!s.has_ckr)
        return {};
    while (s.nr < until) {
        Key32 mk {};
        kdf_ck(s.ckr, mk);
        s.skipped.push_back({ s.hkr, s.nr, mk });
        wipe(mk);
        ++s.nr;
        if (s.skipped.size() > MaxStoredSkipped) {
            wipe(s.skipped.front().hk);
            wipe(s.skipped.front().mk);
            s.skipped.erase(s.skipped.begin());
        }
    }
    return {};
}

Bytes body_ad(const SessionState &s, std::span<const std::uint8_t> enc_header) {
    Bytes ad(s.ad.begin(), s.ad.end());
    ad.push_back(WireVersion);
    put_bytes(ad, enc_header);
    return ad;
}

std::expected<Bytes, Error> ratchet_encrypt(SessionState &s, std::span<const std::uint8_t> plaintext) {
    if (!s.has_cks || !s.has_hks)
        return std::unexpected(Error::NotReady);
    auto padded = pad(plaintext);
    if (!padded.has_value())
        return std::unexpected(Error::TooLarge);

    Key32 mk {};
    kdf_ck(s.cks, mk);

    Header h;
    h.dh = s.dhs_pk;
    h.pn = s.pn;
    h.n  = s.ns;
    h.t  = s.ts;
    const Bytes enc_header = seal_header(s.hks, h);
    ++s.ns;
    ++s.ts;

    const auto nonce = body_nonce(mk);
    const Bytes ad   = body_ad(s, enc_header);
    const Bytes body = aead_seal(*padded, ad, nonce.data(), mk);
    sodium_memzero(padded->data(), padded->size());
    wipe(mk);

    Bytes wire;
    wire.reserve(1 + enc_header.size() + body.size());
    wire.push_back(WireVersion);
    put_bytes(wire, enc_header);
    put_bytes(wire, body);
    return wire;
}

// Works on a copy and commits only when the message authenticates, so a forged or damaged
// message cannot move the ratchet
std::expected<Bytes, Error> ratchet_decrypt(SessionState &state, std::span<const std::uint8_t> wire) {
    if (wire.size() < MinWireSize || wire[0] != WireVersion)
        return std::unexpected(Error::BadFormat);
    const auto enc_header = wire.subspan(1, HeaderWireSize);
    const auto body       = wire.subspan(1 + HeaderWireSize);

    SessionState s = state;

    for (std::size_t i = 0; i < s.skipped.size(); ++i) {
        const auto h = open_header(s.skipped[i].hk, enc_header);
        if (!h.has_value() || h->n != s.skipped[i].n)
            continue;
        const Key32 mk    = s.skipped[i].mk;
        const auto  nonce = body_nonce(mk);
        const Bytes ad    = body_ad(s, enc_header);
        auto        plain = aead_open(body, ad, nonce.data(), mk);
        if (!plain.has_value())
            return std::unexpected(Error::AuthFailed);
        auto text = unpad(std::move(*plain));
        if (!text.has_value())
            return std::unexpected(Error::AuthFailed);
        wipe(s.skipped[i].hk);
        wipe(s.skipped[i].mk);
        s.skipped.erase(s.skipped.begin() + static_cast<std::ptrdiff_t>(i));
        state = s;
        return std::move(*text);
    }

    std::optional<Header> header;
    bool                  new_chain = false;
    if (s.has_hkr)
        header = open_header(s.hkr, enc_header);
    if (!header.has_value()) {
        header = open_header(s.nhkr, enc_header);
        if (header.has_value())
            new_chain = true;
    }
    if (!header.has_value())
        return std::unexpected(Error::AuthFailed);

    if (new_chain) {
        if (auto r = skip_message_keys(s, header->pn); !r.has_value())
            return std::unexpected(r.error());
        if (auto r = dh_ratchet(s, *header); !r.has_value())
            return std::unexpected(r.error());
    } else if (header->n < s.nr) {
        // Already consumed: a replay, or a late message whose key was dropped
        return std::unexpected(Error::AuthFailed);
    }
    if (auto r = skip_message_keys(s, header->n); !r.has_value())
        return std::unexpected(r.error());
    if (!s.has_ckr)
        return std::unexpected(Error::AuthFailed);

    Key32 mk {};
    kdf_ck(s.ckr, mk);
    ++s.nr;
    s.tr = std::max<std::uint64_t>(s.tr, header->t + 1);

    const auto nonce = body_nonce(mk);
    const Bytes ad   = body_ad(s, enc_header);
    auto        plain = aead_open(body, ad, nonce.data(), mk);
    wipe(mk);
    if (!plain.has_value())
        return std::unexpected(Error::AuthFailed);
    auto text = unpad(std::move(*plain));
    if (!text.has_value())
        return std::unexpected(Error::AuthFailed);

    state = s;
    return std::move(*text);
}

Bytes serialize_state(const SessionState &s) {
    Bytes out { 'S', 'C', 'S', 1 };
    put_bytes(out, s.ad);
    put_bytes(out, s.self_identity);
    put_bytes(out, s.peer_identity);
    put_bytes(out, s.dhs_pk);
    put_bytes(out, s.dhs_sk);
    out.push_back(s.has_dhr ? 1 : 0);
    put_bytes(out, s.dhr);
    put_bytes(out, s.rk);
    out.push_back(s.has_cks ? 1 : 0);
    put_bytes(out, s.cks);
    out.push_back(s.has_ckr ? 1 : 0);
    put_bytes(out, s.ckr);
    put_u32(out, s.ns);
    put_u32(out, s.nr);
    put_u32(out, s.pn);
    put_u64(out, s.ts);
    put_u64(out, s.tr);
    out.push_back(s.has_hks ? 1 : 0);
    put_bytes(out, s.hks);
    out.push_back(s.has_hkr ? 1 : 0);
    put_bytes(out, s.hkr);
    put_bytes(out, s.nhks);
    put_bytes(out, s.nhkr);
    put_u32(out, static_cast<std::uint32_t>(s.skipped.size()));
    for (const auto &entry : s.skipped) {
        put_bytes(out, entry.hk);
        put_u32(out, entry.n);
        put_bytes(out, entry.mk);
    }
    return out;
}

bool flag(Reader &r, bool &out) {
    std::uint8_t v = 0;
    if (!r.u8(v) || v > 1)
        return false;
    out = v == 1;
    return true;
}

std::optional<SessionState> parse_state(std::span<const std::uint8_t> data) {
    Reader                     r(data);
    std::array<std::uint8_t, 4> magic {};
    if (!r.take(magic) || magic != std::array<std::uint8_t, 4> { 'S', 'C', 'S', 1 })
        return std::nullopt;
    SessionState s;
    std::uint32_t count = 0;
    const bool    ok = r.take(s.ad) && r.take(s.self_identity) && r.take(s.peer_identity) && r.take(s.dhs_pk)
                    && r.take(s.dhs_sk) && flag(r, s.has_dhr) && r.take(s.dhr) && r.take(s.rk)
                    && flag(r, s.has_cks) && r.take(s.cks) && flag(r, s.has_ckr) && r.take(s.ckr) && r.u32(s.ns)
                    && r.u32(s.nr) && r.u32(s.pn) && r.u64(s.ts) && r.u64(s.tr) && flag(r, s.has_hks)
                    && r.take(s.hks) && flag(r, s.has_hkr) && r.take(s.hkr) && r.take(s.nhks) && r.take(s.nhkr)
                    && r.u32(count);
    if (!ok || count > MaxStoredSkipped)
        return std::nullopt;
    for (std::uint32_t i = 0; i < count; ++i) {
        SessionState::Skipped entry;
        if (!r.take(entry.hk) || !r.u32(entry.n) || !r.take(entry.mk))
            return std::nullopt;
        s.skipped.push_back(entry);
    }
    if (r.remaining() != 0)
        return std::nullopt;
    return s;
}

} // namespace

Session::Session(std::unique_ptr<SessionState> state) : state_(std::move(state)) {}
Session::Session(const Session &other) : state_(std::make_unique<SessionState>(*other.state_)) {}
Session &Session::operator=(const Session &other) {
    if (this != &other)
        state_ = std::make_unique<SessionState>(*other.state_);
    return *this;
}
Session::Session(Session &&) noexcept            = default;
Session &Session::operator=(Session &&) noexcept = default;
Session::~Session()                              = default;

const Key32 &Session::peer_identity() const {
    return state_->peer_identity;
}

std::string Session::safety_number() const {
    return SecretChat::safety_number(state_->self_identity, state_->peer_identity);
}

std::optional<MailboxId> Session::next_send_mailbox_id() const {
    if (!state_->has_hks)
        return std::nullopt;
    return mailbox_id(state_->hks, state_->ts);
}

std::vector<MailboxId> Session::expected_receive_mailbox_ids() const {
    std::vector<MailboxId> ids;
    ids.reserve(MailboxWindow * 2);
    for (std::size_t i = 0; i < MailboxWindow; ++i) {
        if (state_->has_hkr)
            ids.push_back(mailbox_id(state_->hkr, state_->tr + i));
        ids.push_back(mailbox_id(state_->nhkr, state_->tr + i));
    }
    return ids;
}

std::expected<Bytes, Error> Session::encrypt(std::span<const std::uint8_t> plaintext) {
    return ratchet_encrypt(*state_, plaintext);
}

std::expected<Bytes, Error> Session::decrypt(std::span<const std::uint8_t> wire) {
    return ratchet_decrypt(*state_, wire);
}

Bytes Session::serialize() const {
    return serialize_state(*state_);
}

std::expected<Session, Error> Session::deserialize(std::span<const std::uint8_t> data) {
    ensure_sodium();
    auto parsed = parse_state(data);
    if (!parsed.has_value())
        return std::unexpected(Error::BadFormat);
    return Session(std::make_unique<SessionState>(std::move(*parsed)));
}

std::expected<Session::Started, Error> Session::initiate(const IdentityKeys           &self,
                                                         const PreKeyBundle           &peer,
                                                         std::span<const std::uint8_t> first_plaintext) {
    ensure_sodium();
    if (!peer.verify())
        return std::unexpected(Error::BadSignature);

    auto self_x_sk = x25519_secret(self.secret_key);
    auto peer_x    = x25519_public(peer.identity_key);
    if (!self_x_sk.has_value() || !peer_x.has_value())
        return std::unexpected(self_x_sk.has_value() ? peer_x.error() : self_x_sk.error());

    Key32 ek_pk {};
    Key32 ek_sk {};
    new_keypair(ek_pk, ek_sk);

    auto dh1 = dh(*self_x_sk, peer.signed_prekey);
    auto dh2 = dh(ek_sk, *peer_x);
    auto dh3 = dh(ek_sk, peer.signed_prekey);
    wipe(*self_x_sk);
    wipe(ek_sk);
    if (!dh1.has_value() || !dh2.has_value() || !dh3.has_value())
        return std::unexpected(Error::CryptoFailure);
    auto agreement = kdf_x3dh(*dh1, *dh2, *dh3);
    wipe(*dh1);
    wipe(*dh2);
    wipe(*dh3);
    if (!agreement.has_value())
        return std::unexpected(agreement.error());

    auto st           = std::make_unique<SessionState>();
    st->self_identity = self.public_key;
    st->peer_identity = peer.identity_key;
    std::memcpy(st->ad.data(), self.public_key.data(), 32);
    std::memcpy(st->ad.data() + 32, peer.identity_key.data(), 32);

    new_keypair(st->dhs_pk, st->dhs_sk);
    st->dhr     = peer.signed_prekey;
    st->has_dhr = true;
    auto dh_out = dh(st->dhs_sk, st->dhr);
    if (!dh_out.has_value())
        return std::unexpected(dh_out.error());
    auto step = kdf_rk(agreement->root, *dh_out);
    wipe(*dh_out);
    if (!step.has_value())
        return std::unexpected(step.error());
    st->rk      = step->rk;
    st->cks     = step->ck;
    st->has_cks = true;
    st->nhks    = step->nhk;
    st->hks     = agreement->hka;
    st->has_hks = true;
    st->nhkr    = agreement->nhkb;
    wipe(agreement->root);

    auto ratchet_wire = ratchet_encrypt(*st, first_plaintext);
    if (!ratchet_wire.has_value())
        return std::unexpected(ratchet_wire.error());

    Bytes payload;
    put_bytes(payload, self.public_key);
    put_bytes(payload, ek_pk);
    put_u32(payload, peer.signed_prekey_id);
    put_bytes(payload, *ratchet_wire);

    Bytes sealed(payload.size() + crypto_box_SEALBYTES);
    if (crypto_box_seal(sealed.data(), payload.data(), payload.size(), peer.signed_prekey.data()) != 0)
        return std::unexpected(Error::CryptoFailure);

    Bytes wire;
    wire.reserve(1 + sealed.size());
    wire.push_back(WireVersion);
    put_bytes(wire, sealed);

    return Started { Session(std::move(st)), std::move(wire), initial_mailbox_id(peer.signed_prekey) };
}

std::expected<Session::Accepted, Error> Session::accept(const IdentityKeys           &self,
                                                        const SignedPreKey           &prekey,
                                                        std::span<const std::uint8_t> wire) {
    ensure_sodium();
    if (wire.size() < 1 + crypto_box_SEALBYTES + 68 || wire[0] != WireVersion)
        return std::unexpected(Error::BadFormat);
    const auto sealed = wire.subspan(1);

    Bytes payload(sealed.size() - crypto_box_SEALBYTES);
    if (crypto_box_seal_open(payload.data(), sealed.data(), sealed.size(), prekey.public_key.data(), prekey.secret_key.data())
        != 0)
        return std::unexpected(Error::AuthFailed);

    Reader        reader(payload);
    Key32         peer_identity {};
    Key32         ek_pk {};
    std::uint32_t prekey_id = 0;
    if (!reader.take(peer_identity) || !reader.take(ek_pk) || !reader.u32(prekey_id) || prekey_id != prekey.id)
        return std::unexpected(Error::BadFormat);
    const auto ratchet_wire = reader.rest();

    auto peer_x    = x25519_public(peer_identity);
    auto self_x_sk = x25519_secret(self.secret_key);
    if (!peer_x.has_value() || !self_x_sk.has_value())
        return std::unexpected(peer_x.has_value() ? self_x_sk.error() : peer_x.error());

    auto dh1 = dh(prekey.secret_key, *peer_x);
    auto dh2 = dh(*self_x_sk, ek_pk);
    auto dh3 = dh(prekey.secret_key, ek_pk);
    wipe(*self_x_sk);
    if (!dh1.has_value() || !dh2.has_value() || !dh3.has_value())
        return std::unexpected(Error::CryptoFailure);
    auto agreement = kdf_x3dh(*dh1, *dh2, *dh3);
    wipe(*dh1);
    wipe(*dh2);
    wipe(*dh3);
    if (!agreement.has_value())
        return std::unexpected(agreement.error());

    auto st           = std::make_unique<SessionState>();
    st->self_identity = self.public_key;
    st->peer_identity = peer_identity;
    std::memcpy(st->ad.data(), peer_identity.data(), 32);
    std::memcpy(st->ad.data() + 32, self.public_key.data(), 32);
    st->dhs_pk = prekey.public_key;
    st->dhs_sk = prekey.secret_key;
    st->rk     = agreement->root;
    st->nhks   = agreement->nhkb;
    st->nhkr   = agreement->hka;
    wipe(agreement->root);

    auto plaintext = ratchet_decrypt(*st, ratchet_wire);
    if (!plaintext.has_value())
        return std::unexpected(plaintext.error());

    return Accepted { Session(std::move(st)), std::move(*plaintext) };
}

} // namespace SecretChat
