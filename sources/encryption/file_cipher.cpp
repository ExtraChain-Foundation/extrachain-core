#include "encryption/encryption_tools.h"
#include "core/container.h"
#include "utils/file_io.h"

namespace {
    constexpr std::array<std::uint8_t, 8> MAGIC { 'E', 'X', 'C', 'S', 'T', 'R', 'M', '2' };
    constexpr std::size_t                 MAX_CHUNK    = 1024 * 1024;
    constexpr std::size_t                 TAG_BYTES    = crypto_secretstream_xchacha20poly1305_ABYTES;
    constexpr std::size_t                 HEADER_BYTES = crypto_secretstream_xchacha20poly1305_HEADERBYTES;
    enum class Mode : std::uint8_t {
        Symmetric  = 1,
        Asymmetric = 2,
        Password   = 3
    };

    struct StreamState {
        crypto_secretstream_xchacha20poly1305_state value { };
        ~StreamState() {
            sodium_memzero(&value, sizeof(value));
        }
    };

    std::size_t wrapped_size(Mode mode) {
        switch (mode) {
        case Mode::Symmetric:
            return 0;
        case Mode::Asymmetric:
            return 32 + crypto_box_NONCEBYTES + crypto_box_MACBYTES;
        case Mode::Password:
            return 98;
        }
        return 0;
    }

    std::expected<KeyPass, Cryptography::CryptoError> stream_key(Cryptography::CryptoResult bytes) {
        if (!bytes.has_value()) {
            return std::unexpected(bytes.error());
        }
        KeyPass key;
        if (bytes.value().size() != key.size()) {
            sodium_memzero(bytes.value().data(), bytes.value().size());
            return std::unexpected(Cryptography::CryptoError::DecryptionFailed);
        }
        std::copy(bytes.value().begin(), bytes.value().end(), key.begin());
        sodium_memzero(bytes.value().data(), bytes.value().size());
        return key;
    }

    std::expected<bool, FsError> encrypt_stream(const FsPath&  input_path,
                                                const FsPath&  output_path,
                                                const KeyPass& key,
                                                Mode           mode,
                                                const Bytes&   wrapped,
                                                std::size_t    block_size) {
        const auto valid = Cryptography::validate_encryption_paths(input_path, output_path);
        if (!valid.has_value() || !valid.value() || block_size == 0 || block_size > MAX_CHUNK
            || wrapped.size() != wrapped_size(mode) || ExtraChain::Core::all_zero(key)) {
            return std::unexpected(FsError::ValidationError);
        }
        std::ifstream input(input_path.native(), std::ios::binary);
        if (!input) {
            return std::unexpected(FsError::IoError);
        }
        StreamState state;
        Bytes       header(MAGIC.begin(), MAGIC.end());
        header.push_back(std::to_underlying(mode));
        header.push_back(static_cast<std::uint8_t>(wrapped.size() >> 8));
        header.push_back(static_cast<std::uint8_t>(wrapped.size()));
        header.insert(header.end(), wrapped.begin(), wrapped.end());
        const auto stream_header = header.size();
        header.resize(stream_header + HEADER_BYTES);
        if (crypto_secretstream_xchacha20poly1305_init_push(&state.value,
                                                            header.data() + stream_header,
                                                            key.data())
            != 0) {
            return std::unexpected(FsError::IoError);
        }
        const auto written = FileIo::write_private_atomic_stream(output_path.native(), [&](std::FILE* output) {
            if (std::fwrite(header.data(), 1, header.size(), output) != header.size()) {
                return false;
            }
            Bytes plain(block_size);
            Bytes cipher(block_size + TAG_BYTES);
            for (;;) {
                input.read(reinterpret_cast<char*>(plain.data()), plain.size());
                const auto count = static_cast<std::size_t>(input.gcount());
                if (input.bad() || (input.fail() && !input.eof())) {
                    return false;
                }
                const bool last = input.peek() == std::char_traits<char>::eof();
                if (last && (!input.eof() || input.bad())) {
                    return false;
                }
                unsigned long long length = 0;
                const auto         tag    = last ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
                                                 : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
                if (crypto_secretstream_xchacha20poly1305_push(&state.value,
                                                               cipher.data(),
                                                               &length,
                                                               plain.data(),
                                                               count,
                                                               header.data(),
                                                               header.size(),
                                                               tag)
                    != 0) {
                    return false;
                }
                sodium_memzero(plain.data(), count);
                const std::array<std::uint8_t, 4> size { static_cast<std::uint8_t>(length >> 24),
                                                         static_cast<std::uint8_t>(length >> 16),
                                                         static_cast<std::uint8_t>(length >> 8),
                                                         static_cast<std::uint8_t>(length) };
                if (std::fwrite(size.data(), 1, size.size(), output) != size.size()
                    || std::fwrite(cipher.data(), 1, length, output) != length) {
                    return false;
                }
                if (last) {
                    return true;
                }
            }
        });
        return written.has_value() ? std::expected<bool, FsError>(true) : std::unexpected(FsError::IoError);
    }

    using KeyResolver = std::function<std::expected<KeyPass, Cryptography::CryptoError>(const Bytes&)>;

    std::expected<bool, FsError> decrypt_stream(const FsPath&      input_path,
                                                const FsPath&      output_path,
                                                Mode               mode,
                                                const KeyResolver& resolve,
                                                std::size_t        buffer_hint) {
        const auto valid = Cryptography::validate_encryption_paths(input_path, output_path);
        if (!valid.has_value() || !valid.value()) {
            return std::unexpected(FsError::ValidationError);
        }
        std::ifstream input(input_path.native(), std::ios::binary);
        Bytes         header(11);
        if (!input.read(reinterpret_cast<char*>(header.data()), header.size())
            || !std::equal(MAGIC.begin(), MAGIC.end(), header.begin()) || header[8] != std::to_underlying(mode)) {
            return std::unexpected(FsError::IoError);
        }
        const std::size_t key_length = (static_cast<std::size_t>(header[9]) << 8) | header[10];
        if (key_length != wrapped_size(mode)) {
            return std::unexpected(FsError::IoError);
        }
        header.resize(11 + key_length + HEADER_BYTES);
        if (!input.read(reinterpret_cast<char*>(header.data() + 11), key_length + HEADER_BYTES)) {
            return std::unexpected(FsError::IoError);
        }
        const Bytes wrapped(header.begin() + 11, header.begin() + 11 + key_length);
        if (mode == Mode::Password
            && !std::equal(wrapped.begin(),
                           wrapped.begin() + 4,
                           std::array<std::uint8_t, 4> { 'E', 'C', 'P', '2' }.begin())) {
            return std::unexpected(FsError::IoError);
        }
        auto key = resolve(wrapped);
        if (!key.has_value() || ExtraChain::Core::all_zero(key.value())) {
            return std::unexpected(FsError::IoError);
        }
        StreamState state;
        const auto  initialized = crypto_secretstream_xchacha20poly1305_init_pull(&state.value,
                                                                                  header.data() + 11 + key_length,
                                                                                  key.value().data());
        sodium_memzero(key.value().data(), key.value().size());
        if (initialized != 0) {
            return std::unexpected(FsError::IoError);
        }
        const auto written = FileIo::write_private_atomic_stream(output_path.native(), [&](std::FILE* output) {
            Bytes cipher;
            Bytes plain;
            cipher.reserve(std::min(buffer_hint, MAX_CHUNK) + TAG_BYTES);
            plain.reserve(std::min(buffer_hint, MAX_CHUNK));
            for (;;) {
                std::array<std::uint8_t, 4> size;
                if (!input.read(reinterpret_cast<char*>(size.data()), size.size())) {
                    return false;
                }
                const std::uint32_t length = (std::uint32_t(size[0]) << 24) | (std::uint32_t(size[1]) << 16)
                                             | (std::uint32_t(size[2]) << 8) | size[3];
                if (length < TAG_BYTES || length > MAX_CHUNK + TAG_BYTES) {
                    return false;
                }
                cipher.resize(length);
                plain.resize(length - TAG_BYTES);
                if (!input.read(reinterpret_cast<char*>(cipher.data()), cipher.size())) {
                    return false;
                }
                unsigned long long count = 0;
                unsigned char      tag   = 0;
                if (crypto_secretstream_xchacha20poly1305_pull(&state.value,
                                                               plain.data(),
                                                               &count,
                                                               &tag,
                                                               cipher.data(),
                                                               cipher.size(),
                                                               header.data(),
                                                               header.size())
                        != 0
                    || (tag != crypto_secretstream_xchacha20poly1305_TAG_MESSAGE
                        && tag != crypto_secretstream_xchacha20poly1305_TAG_FINAL)) {
                    sodium_memzero(plain.data(), plain.size());
                    return false;
                }
                const bool complete = std::fwrite(plain.data(), 1, count, output) == count;
                sodium_memzero(plain.data(), plain.size());
                if (!complete) {
                    return false;
                }
                if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL) {
                    return input.peek() == std::char_traits<char>::eof() && input.eof() && !input.bad();
                }
            }
        });
        return written.has_value() ? std::expected<bool, FsError>(true) : std::unexpected(FsError::IoError);
    }

    using KeyWrapper = std::function<Cryptography::CryptoResult(const Bytes&)>;
    std::expected<bool, FsError> encrypt_wrapped(const FsPath&     input,
                                                 const FsPath&     output,
                                                 Mode              mode,
                                                 const KeyWrapper& wrap,
                                                 std::size_t       block_size) {
        auto       key = Cryptography::keygen();
        Bytes      raw_key(key.begin(), key.end());
        const auto wrapped = wrap(raw_key);
        sodium_memzero(raw_key.data(), raw_key.size());
        if (!wrapped.has_value()) {
            sodium_memzero(key.data(), key.size());
            return std::unexpected(FsError::IoError);
        }
        const auto result = encrypt_stream(input, output, key, mode, wrapped.value(), block_size);
        sodium_memzero(key.data(), key.size());
        return result;
    }
} // namespace

std::expected<bool, FsError> Cryptography::symmetric_encrypt_file(const FsPath&  input,
                                                                  const FsPath&  output,
                                                                  const KeyPass& key,
                                                                  std::size_t    block_size) {
    return encrypt_stream(input, output, key, Mode::Symmetric, { }, block_size);
}
std::expected<bool, FsError> Cryptography::symmetric_decrypt_file(const FsPath&  input,
                                                                  const FsPath&  output,
                                                                  const KeyPass& key,
                                                                  std::size_t    block_size) {
    return decrypt_stream(
        input,
        output,
        Mode::Symmetric,
        [&](const Bytes&) {
            return std::expected<KeyPass, CryptoError>(key);
        },
        block_size);
}
std::expected<bool, FsError> Cryptography::symmetric_encrypt_file_password(const FsPath&      input,
                                                                           const FsPath&      output,
                                                                           const std::string& password,
                                                                           std::size_t        block_size) {
    return encrypt_wrapped(
        input,
        output,
        Mode::Password,
        [&](const Bytes& key) {
            return symmetric_encrypt_password(key, password);
        },
        block_size);
}
std::expected<bool, FsError> Cryptography::symmetric_decrypt_file_password(const FsPath&      input,
                                                                           const FsPath&      output,
                                                                           const std::string& password,
                                                                           std::size_t        block_size) {
    return decrypt_stream(
        input,
        output,
        Mode::Password,
        [&](const Bytes& wrapped) {
            return stream_key(symmetric_decrypt_password(wrapped, password));
        },
        block_size);
}
std::expected<bool, FsError> Cryptography::asymmetric_encrypt_file(const FsPath&     input,
                                                                   const FsPath&     output,
                                                                   const PrivateKey& sender,
                                                                   const PublicKey&  receiver,
                                                                   std::size_t       block_size) {
    return encrypt_wrapped(
        input,
        output,
        Mode::Asymmetric,
        [&](const Bytes& key) {
            return asymmetric_encrypt(key, sender, receiver);
        },
        block_size);
}
std::expected<bool, FsError> Cryptography::asymmetric_decrypt_file(const FsPath&     input,
                                                                   const FsPath&     output,
                                                                   const PrivateKey& receiver,
                                                                   const PublicKey&  sender,
                                                                   std::size_t       block_size) {
    return decrypt_stream(
        input,
        output,
        Mode::Asymmetric,
        [&](const Bytes& wrapped) {
            return stream_key(asymmetric_decrypt(wrapped, receiver, sender));
        },
        block_size);
}
std::expected<bool, FsError> Cryptography::asymmetric_encrypt_self_file(const FsPath&     input,
                                                                        const FsPath&     output,
                                                                        const PrivateKey& secret,
                                                                        const PublicKey&  public_key,
                                                                        std::size_t       block_size) {
    return asymmetric_encrypt_file(input, output, secret, public_key, block_size);
}
std::expected<bool, FsError> Cryptography::asymmetric_decrypt_self_file(const FsPath&     input,
                                                                        const FsPath&     output,
                                                                        const PrivateKey& secret,
                                                                        const PublicKey&  public_key,
                                                                        std::size_t       block_size) {
    return asymmetric_decrypt_file(input, output, secret, public_key, block_size);
}
