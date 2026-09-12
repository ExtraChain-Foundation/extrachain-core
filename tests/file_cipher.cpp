#include <filesystem>
#include <functional>

#include "encryption/encryption_tools.h"
#include "test_support.h"
#include "utils/exc_utils.h"
#include "utils/file_io.h"

namespace {
    struct Layout {
        std::string              prefix;
        std::vector<std::string> records;
    };
    Layout split(const std::string& cipher) {
        Layout result;
        if (cipher.starts_with("EXCSTRM2")) {
            const auto  wrapped = (std::size_t(static_cast<unsigned char>(cipher.at(9))) << 8)
                                  | static_cast<unsigned char>(cipher.at(10));
            std::size_t offset  = 11 + wrapped + 24;
            result.prefix       = cipher.substr(0, offset);
            while (offset < cipher.size()) {
                std::uint32_t size = 0;
                for (unsigned i = 0; i < 4; ++i) {
                    size = (size << 8) | static_cast<unsigned char>(cipher.at(offset + i));
                }
                TEST_REQUIRE(size <= cipher.size() - offset - 4);
                result.records.push_back(cipher.substr(offset, 4 + size));
                offset += 4 + size;
            }
        } else {
            constexpr std::size_t legacy_record = 72 + 40;
            for (std::size_t offset = 0; offset < cipher.size(); offset += legacy_record) {
                result.records.push_back(cipher.substr(offset, legacy_record));
            }
        }
        return result;
    }
    std::string join(const Layout& layout) {
        auto result = layout.prefix;
        for (const auto& record : layout.records) {
            result += record;
        }
        return result;
    }
    using Cipher = std::function<std::expected<bool, FsError>(const FsPath&, const FsPath&, std::size_t)>;
} // namespace

int main(int argc, char** argv) {
    TEST_REQUIRE(sodium_init() >= 0);
    const bool          integrity_only = argc > 1 && std::string_view(argv[1]) == "integrity";
    TestSupport::Runner tests;
    const auto          original = std::filesystem::current_path();
    const auto          directory =
        std::filesystem::temp_directory_path() / ("extrachain-file-cipher-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    const auto input                              = FsPath::create(std::string_view("input")).value();
    const auto encrypted                          = FsPath::create(std::string_view("encrypted")).value();
    const auto output                             = FsPath::create(std::string_view("output")).value();
    const auto key                                = Cryptography::keygen();
    const auto [sender_secret, sender_public]     = Cryptography::asymmetric_create_pair();
    const auto [receiver_secret, receiver_public] = Cryptography::asymmetric_create_pair();
    const std::vector<std::pair<Cipher, Cipher>> modes {
        { [&](const auto& in, const auto& out, auto size) {
             return Cryptography::symmetric_encrypt_file(in, out, key, size);
         },
          [&](const auto& in, const auto& out, auto size) {
              return Cryptography::symmetric_decrypt_file(in, out, key, size);
          } },
        { [&](const auto& in, const auto& out, auto size) {
             return Cryptography::asymmetric_encrypt_file(in, out, sender_secret, receiver_public, size);
         },
          [&](const auto& in, const auto& out, auto size) {
              return Cryptography::asymmetric_decrypt_file(in, out, receiver_secret, sender_public, size);
          } },
        { [&](const auto& in, const auto& out, auto size) {
             return Cryptography::symmetric_encrypt_file_password(in, out, "file-password", size);
         },
          [&](const auto& in, const auto& out, auto size) {
              return Cryptography::symmetric_decrypt_file_password(in, out, "file-password", size);
          } }
    };
    const auto                wrong_key = Cryptography::keygen();
    const std::vector<Cipher> wrong_decrypt {
        [&](const auto& in, const auto& out, auto size) {
            return Cryptography::symmetric_decrypt_file(in, out, wrong_key, size);
        },
        [&](const auto& in, const auto& out, auto size) {
            return Cryptography::asymmetric_decrypt_file(in, out, sender_secret, sender_public, size);
        },
        [&](const auto& in, const auto& out, auto size) {
            return Cryptography::symmetric_decrypt_file_password(in, out, "wrong-password", size);
        }
    };
    for (std::size_t mode = 0; mode < modes.size(); ++mode) {
        const auto& [encrypt, decrypt] = modes[mode];
        if (!integrity_only) {
            tests.run("stream round trips include empty and partial blocks", [&] {
                for (std::size_t size : { 0U, 1U, 64U, 65U, 199U, 4113U }) {
                    std::string data(size, '\0');
                    for (std::size_t i = 0; i < size; ++i) {
                        data[i] = static_cast<char>(i % 251);
                    }
                    TEST_REQUIRE(FileIo::write_atomic(input.native(), data).has_value());
                    const auto saved = encrypt(input, encrypted, 64);
                    TEST_REQUIRE(saved.has_value() && saved.value());
                    const auto restored = decrypt(encrypted, output, 64);
                    TEST_REQUIRE(restored.has_value() && restored.value());
                    TEST_REQUIRE_EQ(FileIo::read_all(output.native()).value(), data);
                }
                const auto first = FileIo::read_all(encrypted.native()).value();
                TEST_REQUIRE(encrypt(input, encrypted, 64).has_value());
                TEST_REQUIRE(first != FileIo::read_all(encrypted.native()).value());
                TEST_REQUIRE(FileIo::write_atomic(output.native(), "keep the existing file").has_value());
                TEST_REQUIRE(!wrong_decrypt[mode](encrypted, output, 64).has_value());
                TEST_REQUIRE_EQ(FileIo::read_all(output.native()).value(), "keep the existing file");
                TEST_REQUIRE(!encrypt(input, encrypted, 0).has_value());
                TEST_REQUIRE(!encrypt(input, encrypted, 1024 * 1024 + 1).has_value());
            });
        }
        tests.run("stream corruption preserves the destination", [&] {
            TEST_REQUIRE(FileIo::write_atomic(input.native(), std::string(199, 'a')).has_value());
            TEST_REQUIRE(encrypt(input, encrypted, 64).has_value());
            const auto good   = FileIo::read_all(encrypted.native()).value();
            const auto layout = split(good);
            TEST_REQUIRE(layout.records.size() >= 3);
            std::vector<std::string> corrupt;
            auto                     changed = layout;
            std::swap(changed.records[0], changed.records[1]);
            corrupt.push_back(join(changed));
            changed = layout;
            changed.records.pop_back();
            corrupt.push_back(join(changed));
            changed = layout;
            changed.records.insert(changed.records.begin(), changed.records.front());
            corrupt.push_back(join(changed));
            TEST_REQUIRE(FileIo::write_atomic(input.native(), std::string(199, 'b')).has_value());
            TEST_REQUIRE(encrypt(input, encrypted, 64).has_value());
            const auto other   = split(FileIo::read_all(encrypted.native()).value());
            changed            = layout;
            changed.records[1] = other.records[1];
            corrupt.push_back(join(changed));
            corrupt.push_back(good + layout.records.front());
            corrupt.push_back(good.substr(0, good.size() - 1));
            auto flipped = good;
            flipped.back() ^= 1;
            corrupt.push_back(flipped);
            if (!layout.prefix.empty()) {
                auto oversized = good;
                oversized.replace(layout.prefix.size(), 4, std::string(4, '\xff'));
                corrupt.push_back(oversized);
                auto wrong_mode = good;
                wrong_mode[8]   = static_cast<char>(99);
                corrupt.push_back(wrong_mode);
            }
            for (const auto& data : corrupt) {
                TEST_REQUIRE(FileIo::write_atomic(encrypted.native(), data).has_value());
                TEST_REQUIRE(FileIo::write_atomic(output.native(), "keep the existing file").has_value());
                TEST_REQUIRE(!decrypt(encrypted, output, 64).has_value());
                TEST_REQUIRE_EQ(FileIo::read_all(output.native()).value(), "keep the existing file");
                for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                    TEST_REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
                }
            }
        });
    }
    std::filesystem::current_path(original);
    const auto result = tests.result();
    if (result == 0) {
        std::filesystem::remove_all(directory);
    }
    return result;
}
