#include <thread>

#include "encryption/encryption_tools.h"
#include "test_support.h"

namespace {
    struct KeyPair {
        PublicKey  public_key;
        PrivateKey private_key;
        KeyPair() {
            TEST_REQUIRE(crypto_sign_keypair(public_key.data(), private_key.data()) == 0);
        }
    };
} // namespace

int main() {
    TEST_REQUIRE(sodium_init() >= 0);
    const KeyPair sender, other;
    for (const std::size_t size : { 1, 32, 512, 513, 4096 }) {
        Bytes      payload(size, 19);
        const auto signed_payload = Cryptography::sign(payload, sender.private_key);
        TEST_REQUIRE(signed_payload.has_value());
        for (std::size_t repeat = 0; repeat < 2; ++repeat) {
            const auto verified = Cryptography::verify(payload, sender.public_key, signed_payload.value());
            TEST_REQUIRE(verified.has_value() && verified.value());
        }
        auto altered = payload;
        altered.back() ^= 1;
        const auto wrong_payload = Cryptography::verify(altered, sender.public_key, signed_payload.value());
        TEST_REQUIRE(wrong_payload.has_value() && !wrong_payload.value());
        const auto wrong_signer = Cryptography::verify(payload, other.public_key, signed_payload.value());
        TEST_REQUIRE(wrong_signer.has_value() && !wrong_signer.value());
        auto altered_signature = signed_payload.value();
        altered_signature.back() ^= 1;
        const auto wrong_signature = Cryptography::verify(payload, sender.public_key, altered_signature);
        TEST_REQUIRE(wrong_signature.has_value() && !wrong_signature.value());
        const auto valid_after_invalid = Cryptography::verify(payload, sender.public_key, signed_payload.value());
        TEST_REQUIRE(valid_after_invalid.has_value() && valid_after_invalid.value());
    }
    const Bytes original_payload { 1, 2, 3 };
    const auto  original_signature = Cryptography::sign(original_payload, sender.private_key);
    TEST_REQUIRE(original_signature.has_value());
    const auto original_verified =
        Cryptography::verify(original_payload, sender.public_key, original_signature.value());
    TEST_REQUIRE(original_verified.has_value() && original_verified.value());
    for (std::size_t value = 0; value < 1100; ++value) {
        Bytes      payload { static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8), 9 };
        const auto signature = Cryptography::sign(payload, sender.private_key);
        TEST_REQUIRE(signature.has_value());
        const auto verified = Cryptography::verify(payload, sender.public_key, signature.value());
        TEST_REQUIRE(verified.has_value() && verified.value());
    }
    const auto after_pressure =
        Cryptography::verify(original_payload, sender.public_key, original_signature.value());
    TEST_REQUIRE(after_pressure.has_value() && after_pressure.value());
    const auto empty_payload = Cryptography::verify({}, sender.public_key, original_signature.value());
    TEST_REQUIRE(!empty_payload.has_value() && empty_payload.error() == Cryptography::CryptoError::EmptyData);
    const auto empty_public_key = Cryptography::verify(original_payload, {}, original_signature.value());
    TEST_REQUIRE(!empty_public_key.has_value() && empty_public_key.error() == Cryptography::CryptoError::EmptyKey);
    const auto empty_signature = Cryptography::verify(original_payload, sender.public_key, {});
    TEST_REQUIRE(!empty_signature.has_value() && empty_signature.error() == Cryptography::CryptoError::EmptySign);
    std::vector<std::thread> workers;
    for (std::size_t index = 0; index < 8; ++index) {
        workers.emplace_back([&, index] {
            Bytes      payload(32, static_cast<std::uint8_t>(index + 1));
            const auto signature = Cryptography::sign(payload, sender.private_key);
            TEST_REQUIRE(signature.has_value());
            for (std::size_t repeat = 0; repeat < 128; ++repeat) {
                const auto valid = Cryptography::verify(payload, sender.public_key, signature.value());
                TEST_REQUIRE(valid.has_value() && valid.value());
                auto altered = payload;
                altered.back() ^= 1;
                const auto invalid = Cryptography::verify(altered, sender.public_key, signature.value());
                TEST_REQUIRE(invalid.has_value() && !invalid.value());
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    std::puts("cryptographic verification: PASS");
}
