#include <thread>

#include "encryption/encryption_tools.h"
#include "encryption/box_session.h"
#include "test_support.h"

namespace {
    struct KeyPair {
        PublicKey  public_key;
        PrivateKey private_key;

        KeyPair() {
            TEST_REQUIRE(crypto_sign_keypair(public_key.data(), private_key.data()) == 0);
        }
    };

    void round_trip(const KeyPair& sender, const KeyPair& receiver, const Bytes& input) {
        const auto encrypted = Cryptography::asymmetric_encrypt(input, sender.private_key, receiver.public_key);
        TEST_REQUIRE(encrypted.has_value());
        const auto decrypted =
            Cryptography::asymmetric_decrypt(encrypted.value(), receiver.private_key, sender.public_key);
        TEST_REQUIRE(decrypted.has_value() && decrypted.value() == input);

        Curve25519Key sender_public, sender_private, receiver_public, receiver_private;
        TEST_REQUIRE(crypto_sign_ed25519_pk_to_curve25519(sender_public.data(), sender.public_key.data()) == 0);
        TEST_REQUIRE(crypto_sign_ed25519_sk_to_curve25519(sender_private.data(), sender.private_key.data()) == 0);
        TEST_REQUIRE(crypto_sign_ed25519_pk_to_curve25519(receiver_public.data(), receiver.public_key.data())
                     == 0);
        TEST_REQUIRE(crypto_sign_ed25519_sk_to_curve25519(receiver_private.data(), receiver.private_key.data())
                     == 0);
        Bytes direct_plaintext(input.size());
        TEST_REQUIRE(crypto_box_open_easy(direct_plaintext.data(),
                                          encrypted.value().data() + crypto_box_NONCEBYTES,
                                          encrypted.value().size() - crypto_box_NONCEBYTES,
                                          encrypted.value().data(),
                                          sender_public.data(),
                                          receiver_private.data())
                     == 0);
        TEST_REQUIRE(direct_plaintext == input);

        Bytes direct_ciphertext(crypto_box_NONCEBYTES + crypto_box_MACBYTES + input.size());
        randombytes_buf(direct_ciphertext.data(), crypto_box_NONCEBYTES);
        TEST_REQUIRE(crypto_box_easy(direct_ciphertext.data() + crypto_box_NONCEBYTES,
                                     input.data(),
                                     input.size(),
                                     direct_ciphertext.data(),
                                     receiver_public.data(),
                                     sender_private.data())
                     == 0);
        const auto direct_decrypted =
            Cryptography::asymmetric_decrypt(direct_ciphertext, receiver.private_key, sender.public_key);
        TEST_REQUIRE(direct_decrypted.has_value() && direct_decrypted.value() == input);
        direct_ciphertext.back() ^= 1;
        const auto corrupt =
            Cryptography::asymmetric_decrypt(direct_ciphertext, receiver.private_key, sender.public_key);
        TEST_REQUIRE(!corrupt.has_value() && corrupt.error() == Cryptography::CryptoError::DecryptionFailed);
    }
} // namespace

int main() {
    TEST_REQUIRE(sodium_init() >= 0);
    const KeyPair sender, receiver, other;
    for (const std::size_t size : { 1, 32, 4096 }) {
        Bytes input(size);
        for (std::size_t i = 0; i < size; ++i)
            input[i] = (i * 131 + 17) & 255;
        round_trip(sender, receiver, input);
        round_trip(sender, receiver, input);
    }
    const Bytes input { 0, 1, 255 };
    const auto  encrypted = Cryptography::asymmetric_encrypt(input, sender.private_key, receiver.public_key);
    TEST_REQUIRE(encrypted.has_value());
    const auto wrong_key =
        Cryptography::asymmetric_decrypt(encrypted.value(), receiver.private_key, other.public_key);
    TEST_REQUIRE(!wrong_key.has_value() && wrong_key.error() == Cryptography::CryptoError::DecryptionFailed);
    const PublicKey invalid_key {};
    const auto      invalid = Cryptography::asymmetric_encrypt(input, sender.private_key, invalid_key);
    TEST_REQUIRE(!invalid.has_value() && invalid.error() == Cryptography::CryptoError::KeyConversionFailed);
    const auto invalid_sender =
        Cryptography::asymmetric_decrypt(encrypted.value(), receiver.private_key, invalid_key);
    TEST_REQUIRE(!invalid_sender.has_value()
                 && invalid_sender.error() == Cryptography::CryptoError::KeyConversionFailed);
    const auto short_data = Cryptography::asymmetric_decrypt(Bytes { 0 }, receiver.private_key, sender.public_key);
    TEST_REQUIRE(!short_data.has_value() && short_data.error() == Cryptography::CryptoError::DataTooShort);
    for (std::size_t i = 0; i < 300; ++i) {
        const KeyPair next;
        round_trip(sender, next, input);
    }
    round_trip(sender, receiver, input);
    const auto sender_session   = Cryptography::BoxSession::create(sender.private_key, receiver.public_key);
    const auto receiver_session = Cryptography::BoxSession::create(receiver.private_key, sender.public_key);
    const auto other_session    = Cryptography::BoxSession::create(other.private_key, sender.public_key);
    TEST_REQUIRE(sender_session.has_value() && receiver_session.has_value() && other_session.has_value());
    for (const std::size_t size : { 1, 32, 4096, 262144 }) {
        Bytes plaintext(size);
        for (std::size_t i = 0; i < size; ++i)
            plaintext[i] = (i * 131 + 17) & 255;
        auto packet = sender_session.value()->encrypt(plaintext);
        TEST_REQUIRE(packet.has_value());
        const auto decoded = receiver_session.value()->decrypt(packet.value());
        TEST_REQUIRE(decoded.has_value() && decoded.value() == plaintext);
        const auto old_decoded =
            Cryptography::asymmetric_decrypt(packet.value(), receiver.private_key, sender.public_key);
        TEST_REQUIRE(old_decoded.has_value() && old_decoded.value() == plaintext);
        const auto old_packet =
            Cryptography::asymmetric_encrypt(plaintext, sender.private_key, receiver.public_key);
        TEST_REQUIRE(old_packet.has_value());
        const auto new_decoded = receiver_session.value()->decrypt(old_packet.value());
        TEST_REQUIRE(new_decoded.has_value() && new_decoded.value() == plaintext);
        const auto wrong = other_session.value()->decrypt(packet.value());
        TEST_REQUIRE(!wrong.has_value() && wrong.error() == Cryptography::CryptoError::DecryptionFailed);
        packet.value().back() ^= 1;
        const auto altered = receiver_session.value()->decrypt(packet.value());
        TEST_REQUIRE(!altered.has_value() && altered.error() == Cryptography::CryptoError::DecryptionFailed);
    }
    TEST_REQUIRE(!Cryptography::BoxSession::create(sender.private_key, PublicKey {}).has_value());
    TEST_REQUIRE(!sender_session.value()->encrypt(Bytes {}).has_value());
    TEST_REQUIRE(!receiver_session.value()->decrypt(Bytes { 0 }).has_value());
    std::vector<std::thread> workers;
    for (std::size_t thread = 0; thread < 4; ++thread) {
        workers.emplace_back([&] {
            for (std::size_t i = 0; i < 100; ++i) {
                const auto packet = sender_session.value()->encrypt(input);
                TEST_REQUIRE(packet.has_value());
                const auto plaintext = receiver_session.value()->decrypt(packet.value());
                TEST_REQUIRE(plaintext.has_value() && plaintext.value() == input);
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    std::puts("asymmetric encryption verification: PASS");
}
