#pragma once

#include <memory>
#include <span>

#include "encryption/encryption_tools.h"

namespace Cryptography {
    class EXTRACHAIN_EXPORT BoxSession final {
    public:
        static std::expected<std::unique_ptr<BoxSession>, CryptoError> create(const PrivateKey& local_key,
                                                                              const PublicKey&  peer_key);
        ~BoxSession();
        BoxSession(const BoxSession&)            = delete;
        BoxSession& operator=(const BoxSession&) = delete;

        CryptoResult encrypt(std::span<const std::uint8_t> data) const;
        CryptoResult decrypt(std::span<const std::uint8_t> data) const;

    private:
        BoxSession() = default;
        std::array<std::uint8_t, crypto_box_BEFORENMBYTES> key_ {};
    };
} // namespace Cryptography
