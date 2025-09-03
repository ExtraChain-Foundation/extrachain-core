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

#include <sstream>
#include <string>
#include <vector>
#include <ranges>
#include <algorithm>
#include <expected>
#include <charconv>
#include <system_error>
#include <concepts>
#include <random>

#include <QFile>
#include <QObject>
#include <QtNetwork/QNetworkAddressEntry>

#include "extrachain_global.h"
#include "utils/exc_logs.h"
#include "utils/bignumber_float.h"

#include <msgpack.hpp>
#include "exc_msgpack_describe.h"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/split.hpp>

#include <fmt/chrono.h>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/os.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include "utils/exc_magic.h"
#include <magic_enum/magic_enum.hpp>
#include <magic_enum/magic_enum_iostream.hpp>
using namespace magic_enum::ostream_operators;
using namespace magic_enum::bitwise_operators;

#include <blake3.h>

#include "utils/exc_utils_base64.h"
#include "utils/fs_path.h"

enum class DagMode {
    Full,
    Light
};

enum class DfsMode {
    Full,
    Light
};

enum class Force {
    None,
    Active
};

struct ExtraChainSettings {
    std::optional<std::string> first_node;
    std::optional<DagMode>     dag_mode;
    std::optional<DfsMode>     dfs_mode;
    std::optional<std::string> network_identifier;
};
BOOST_DESCRIBE_STRUCT(ExtraChainSettings, (), (first_node, dag_mode, dfs_mode, network_identifier))

class ByteArray {
public:
    template <size_t N>
    ByteArray(const std::array<uint8_t, N> &arr)
        : m_data(arr.begin(), arr.end()) {
    }

    ByteArray(const std::vector<uint8_t> &vec)
        : m_data(vec) {
    }

    ByteArray(const std::string &str)
        : m_data(reinterpret_cast<const uint8_t *>(str.data()),
                 reinterpret_cast<const uint8_t *>(str.data()) + str.size()) {
    }

    ByteArray(const char *data, size_t length)
        : m_data(reinterpret_cast<const uint8_t *>(data), reinterpret_cast<const uint8_t *>(data) + length) {
    }

    ByteArray(const char *data)
        : ByteArray(data, std::strlen(data)) {
    }

    ByteArray(const QByteArray &qba)
        : m_data(reinterpret_cast<const uint8_t *>(qba.data()),
                 reinterpret_cast<const uint8_t *>(qba.data()) + qba.size()) {
    }

    ByteArray(const QString &qstr)
        : ByteArray(qstr.toUtf8()) {
    }

    template <size_t N>
    std::array<uint8_t, N> toArray() const {
        std::array<uint8_t, N> result {};
        std::copy_n(m_data.begin(), std::min(N, m_data.size()), result.begin());
        return result;
    }

    std::vector<uint8_t> toBytes() const {
        return m_data;
    }

    std::vector<uint8_t> toVector() const {
        return m_data;
    }

    std::string toString() const {
        return std::string(reinterpret_cast<const char *>(m_data.data()), m_data.size());
    }

    QByteArray toQByteArray() const {
        return QByteArray(reinterpret_cast<const char *>(m_data.data()), m_data.size());
    }

    QString toQString() const {
        return QString::fromUtf8(toQByteArray());
    }

    size_t size() const {
        return m_data.size();
    }
    bool empty() const {
        return m_data.empty();
    }
    const uint8_t *data() const {
        return m_data.data();
    }
    uint8_t *data() {
        return m_data.data();
    }

    auto begin() {
        return m_data.begin();
    }
    auto end() {
        return m_data.end();
    }
    auto begin() const {
        return m_data.begin();
    }
    auto end() const {
        return m_data.end();
    }

    uint8_t &operator[](size_t i) {
        return m_data[i];
    }
    const uint8_t &operator[](size_t i) const {
        return m_data[i];
    }

    bool operator==(const ByteArray &other) const {
        return m_data == other.m_data;
    }

    ByteArray operator+(const ByteArray &other) const {
        std::vector<uint8_t> result = m_data;
        result.insert(result.end(), other.m_data.begin(), other.m_data.end());
        return ByteArray(result);
    }

    static ByteArray fromBase64(const std::string &encoded) {
        auto decoded = Utils::from_base64(encoded);
        if (!decoded.has_value()) {
            eFatal("Incorrect base64 in: {}", encoded);
            return ByteArray("");
        }
        return ByteArray(decoded.value());
    }

    static ByteArray fromBase64(const QString &encoded) {
        return fromBase64(encoded.toStdString());
    }

    std::string toBase64() const {
        return Utils::to_base64(toString());
    }

    QString toBase64QString() const {
        return QString::fromStdString(toBase64());
    }

    ByteArray slice(size_t start, size_t length) const {
        return ByteArray(std::vector<uint8_t>(m_data.begin() + start,
                                              m_data.begin() + std::min(start + length, m_data.size())));
    }

private:
    std::vector<uint8_t> m_data;
};

namespace Network {
    Q_NAMESPACE

    static bool    isStartedServer = true;
    static quint16 maxConnections =
#ifdef IS_RC
    #if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        3
    #else
        4
    #endif
#else
        1000
#endif

        ;
    static bool networkDebug = false;

    enum class Protocol {
        Undefined = 0,
        Udp       = 1,
        WebSocket = 2
    };
    Q_ENUM_NS(Protocol)

    enum class SocketServiceError {
        Unknown,
        VersionTooOld,
        VersionTooNew,
        IncompatibleNetwork,
        IncompatibleIdentifier,
        DuplicateIdentifier,
        IncorrectPublicKey,
        IncorrectFirstMessage,
        MaxConnections,
        PeerUnavailable,
        EmptyMessage,
        IncorrectMessage,
        CantSend,
        PhysicalKill,
        IncorrectHandshake,
        PongLost,
        Secs10Inactive
    };
    Q_ENUM_NS(SocketServiceError)
} // namespace Network

namespace Config {
    const int NECESSARY_SAME_TX = 1;

    namespace DataStorage {
        constexpr char DagCacheTable[] = "balance_cache";

        // SQL statement to create the cache table
        constexpr char DagCacheCreate[] = R"(
CREATE TABLE IF NOT EXISTS balance_cache (
    actor_id TEXT NOT NULL,
    token_id TEXT NOT NULL,
    balance TEXT NOT NULL,
    PRIMARY KEY(actor_id, token_id)
);
)";

        static const std::string BlockTable = "Block";
        static const std::string BlockTableCreate = "CREATE TABLE IF NOT EXISTS " + BlockTable
                                            + " ( "
                                              "type         TEXT  NOT NULL, "
                                              "id           TEXT  NOT NULL, "
                                              "date         TEXT  NOT NULL, "
                                              "data         TEXT          , "
                                              "prevHash     TEXT  NOT NULL, "
                                              "hash         TEXT  NOT NULL  "
                                              ");";
        static const std::string TxBlockTable = "Transactions";
        static const std::string TxBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + TxBlockTable
                                              + " ("
                                                "type         INT   NOT NULL, "
                                                "sender       TEXT  NOT NULL, "
                                                "receiver     TEXT  NOT NULL, "
                                                "amount       TEXT  NOT NULL, "
                                                "data         TEXT          , "
                                                "token        TEXT  NOT NULL, "
                                                "prev_block    TEXT  NOT NULL, "
                                                "hash         TEXT  NOT NULL, "
                                                "signature    TEXT  NOT NULL "
                                                ");";
        static const std::string SignTable = "Signatures";
        static const std::string SignBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + SignTable
                                                + " ("
                                                  "actorId      TEXT PRIMARY KEY NOT NULL, "
                                                  "signature    TEXT             NOT NULL, "
                                                  "isApprove    INTEGER CHECK(isApprove IN (0, 1))"
                                                  ");";

        static const std::string GenesisBlockTable = "GenesisBlock";
        static const std::string GenesisBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + GenesisBlockTable
                                                   + " ("
                                                     "type         TEXT  NOT NULL, "
                                                     "id           TEXT  NOT NULL, "
                                                     "date         TEXT  NOT NULL, "
                                                     "data         TEXT          , "
                                                     "prevHash     TEXT  NOT NULL, "
                                                     "hash         TEXT  NOT NULL, "
                                                     "prevGenHash  TEXT            "
                                                     ");";
        static const std::string RowGenesisBlockTable = "GenesisDataRow";
        static const std::string RowGenesisBlockTableCreate = "CREATE TABLE IF NOT EXISTS " + RowGenesisBlockTable
                                                      + " ("
                                                        "actorId    TEXT  NOT NULL, "
                                                        "state      TEXT  NOT NULL, "
                                                        "token      TEXT  NOT NULL, "
                                                        "type       TEXT  NOT NULL "
                                                        ");";

        static const std::string tokensCacheTable = "Tokens";
        static const std::string tokensCacheTableCreate = "CREATE TABLE IF NOT EXISTS " + tokensCacheTable
                                                  + " ("
                                                    "tokenId      TEXT PRIMARY KEY NOT NULL, "
                                                    "name         TEXT             NOT NULL, "
                                                    "color        TEXT             NOT NULL, "
                                                    "canStaking   INT              NOT NULL  "
                                                    ");";

        static const std::string actorsTable = "Actors";
        static const std::string actorsTableCreate = "CREATE TABLE IF NOT EXISTS " + actorsTable
                                             + " ("
                                               "id   TEXT PRIMARY KEY NOT NULL, "
                                               "type INT              NOT NULL  "
                                               ");";

        static const std::string TX_CACHE_TABLE = "Transactions";
        static const std::string TX_CACHE_CREATE = "CREATE TABLE IF NOT EXISTS " + TX_CACHE_TABLE
                                                      + " ("
                                                      "type         INT   NOT NULL, "
                                                      "sender       TEXT  NOT NULL, "
                                                      "receiver     TEXT  NOT NULL, "
                                                      "amount       TEXT  NOT NULL, "
                                                      "meta         TEXT          , "
                                                      "token        TEXT  NOT NULL, "
                                                      "timestamp    TEXT  NOT NULL, "
                                                      "section      TEXT  NOT NULL, "
                                                      "hash         TEXT  NOT NULL UNIQUE, "
                                                      "signature    TEXT  NOT NULL "
                                                      ");";

        static const std::string notificationTable = "Notifications";
        static const std::string notificationTableCreate = "CREATE TABLE IF NOT EXISTS " + notificationTable
                                                     + " ("
                                                     "type            INT    NOT NULL, "
                                                     "amount          TEXT   NOT NULL, "
                                                     "sender          TEXT   NOT NULL, "
                                                     "receiver        TEXT   NOT NULL, "
                                                     "hash            TEXT   NOT NULL, "
                                                     "timestamp       INT    NOT NULL, "
                                                     "message         TEXT"
                                                     ");";

        static const std::string cacheStatusTransactionTable = "CacheStatusTransactions";
        static const std::string cacheStatusTransactionTableCreate = "CREATE TABLE IF NOT EXISTS " + cacheStatusTransactionTable
                                                           + " ("
                                                           "hash              TEXT   NOT NULL, "
                                                           "status            INT    NOT NULL"
                                                           ");";

        // How many files one section folder will store
        static const BigNumber SECTION_SIZE = BigNumber(10000);

        // How often to construct block from pending transactions (in miliseconds)
        static const int BLOCK_CREATION_PERIOD = 5000;

        // How often to construct genesis block (in blocks)
        static const int CONSTRUCT_GENESIS_EVERY_BLOCKS = 20;

        // How often to prove pransactions
        static const int PROVE_TXS_INTERVAL = 2000;

        static int MAX_SIGN_AMOUNT = 13;
    } // namespace DataStorage

    namespace Net {
        // Networking will work only if there are enough peers
        static const int MINIMUM_PEERS = 1;

        // Get Message is considered successful only after NECESSARY_RESPONSE_COUNT
        // responses
        static const int NECESSARY_RESPONSE_COUNT = 1; // 3
    } // namespace Net
} // namespace Config

namespace Serialization {
    EXTRACHAIN_EXPORT std::string serialize(const std::vector<std::string> &list);
    EXTRACHAIN_EXPORT std::vector<std::string> deserialize(const std::string &serialized);
} // namespace Serialization

namespace MessagePack {
    template <class T>
    std::string serialize(const T &t) {
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, t);
        return std::string(buffer.data(), buffer.size());
    }

    enum class DeserializeError {
        EmptyData,
        DeserializationFailed,
    };

    template <class T, class StringContainer>
    std::expected<T, DeserializeError> deserialize(const StringContainer &data, std::size_t size = 0) {
        if (data.empty()) {
            eLog("[MessagePack] Empty deserialize {}", typeid(T).name());
            return std::unexpected(DeserializeError::EmptyData);
        }

        try {
            msgpack::object_handle oh           = msgpack::unpack(data.data(), data.size());
            msgpack::object        deserialized = oh.get();
            return deserialized.as<T>();
        } catch (const std::exception &e) {
            // eWarning("[MessagePack] Exception error: {}", e.what());

            auto qt_bytes = QByteArray::fromStdString(data.data());
            // eWarning("[MessagePack] Incorrect deserialize for {} {}", qt_bytes.toBase64(), qt_bytes);

            return std::unexpected(DeserializeError::DeserializationFailed);
        }
    }

    template <class T>
    std::vector<std::string> serialize_container(std::vector<T> &list) {
        std::vector<std::string> result;
        for (const auto &item : list) {
            result.push_back(serialize(item));
        }
        return result;
    }

    template <class T>
    std::expected<std::vector<T>, DeserializeError> deserialize_container(
        const std::vector<std::string> dataContainer) {
        std::vector<T> result;

        for (const auto &data : dataContainer) {
            const auto element = deserialize<T>(data);
            if (!element.has_value())
                continue;
            result.push_back(element.value());
        }

        return result;
    }
} // namespace MessagePack

namespace Json {
    template <typename T>
    boost::json::value serialize_value(const T &t) {
        auto json = json_convert::to_json(t);
        return json;
    }

    template <typename T>
    std::string serialize(const T &t) {
        auto json     = serialize_value(t);
        auto json_str = boost::json::serialize(json);
        return json_str;
    }

    template <typename T>
    std::expected<T, std::string> deserialize(std::string_view data) {
        try {
            auto parsed = boost::json::parse(data);
            return json_convert::from_json<T>(parsed);
        } catch (const std::exception &e) {
            eWarning("Json deserialize error: {}, data: {}", e.what(), data);
            return std::unexpected(e.what());
        }
    }

    template <typename T>
    std::expected<T, std::string> deserialize(const std::string &data) {
        return deserialize<T>(std::string_view(data));
    }

    template <typename T>
    std::expected<T, std::string> deserialize(const std::vector<uint8_t> &data) {
        return deserialize<T>(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()));
    }

    template <typename T>
    std::expected<T, std::string> _no_try_deserialize(std::string_view data) {
        // for debug
        auto parsed = boost::json::parse(data);
        return json_convert::from_json<T>(parsed);
    }
} // namespace Json

namespace Utils {
    EXTRACHAIN_EXPORT void prepare_extrachain();

    EXTRACHAIN_EXPORT std::string platformDelimeter();
    const static int              RECONNECT_INTERVAL = 5000;
    // Notifications
    static const std::string NOTIFIACATION_CACHE      = "tmp/NotificationCache.db";
    static const std::string TRANSACTION_STATUS_CACHE = "tmp/TrxCache.db";

    // static std::uint64_t current_date_secs() {
    //     using namespace std::chrono;
    //     std::uint64_t secs = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    //     return secs;
    // }

    static std::uint64_t current_date_ms() {
        using namespace std::chrono;
        std::uint64_t ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        return ms;
    }

    template <typename T>
    bool vector_contains(const std::vector<T> &vec, const T &element) {
        return std::find(vec.begin(), vec.end(), element) != vec.end();
    }

    EXTRACHAIN_EXPORT std::string extrachainVersion();
    EXTRACHAIN_EXPORT std::string sodiumVersion();
    EXTRACHAIN_EXPORT std::string boostVersion();
    EXTRACHAIN_EXPORT std::string boostAsioVersion();

    enum class NumberParseError {
        InvalidFormat,
        OutOfRange,
        Empty
    };

    // Concept to restrict numeric types
    template <typename T>
    concept Numeric = std::integral<T> || std::floating_point<T>;

    // Generic parse function for any numeric type
    template <Numeric T>
    std::expected<T, NumberParseError> parse_number(std::string_view str) {
        if (str.empty()) {
            eWarning("Attempted to parse empty string");
            return std::unexpected(NumberParseError::Empty);
        }

        T result {};
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), result);

        if (ec == std::errc::invalid_argument) {
            eWarning("Invalid format while parsing number from string: '{}'", str);
            return std::unexpected(NumberParseError::InvalidFormat);
        }

        if (ec == std::errc::result_out_of_range) {
            eWarning("Number out of range while parsing from string: '{}'", str);
            return std::unexpected(NumberParseError::OutOfRange);
        }

        // Check if we consumed all characters
        if (ptr != str.data() + str.size()) {
            eWarning("Extra characters found while parsing number from string: '{}'", str);
            return std::unexpected(NumberParseError::InvalidFormat);
        }

        return result;
    }

    enum PrintDebug {
        Off = 0,
        On  = 1
    };

    enum class HashAlgorithm {
        Blake3
    };

    enum class ParseError {
        Invalid,
        EmptyString,
        InvalidFormat,
        OutOfRange,
        EnumConversionError,
        FieldNotFound
    };

#ifdef Q_OS_WIN
    static const std::wstring filePrefix = L"file:///";
#else
    static const std::wstring filePrefix = L"file://";
#endif

    template <typename E>
    std::string enum_value_name(E value) {
        return std::string(magic_enum::enum_type_name<E>()) + "::" + std::string(magic_enum::enum_name(value));
    }

    template <typename E>
    std::string enum_value_name_value(E value) {
        return std::string(magic_enum::enum_name(value));
    }

    EXTRACHAIN_EXPORT QString dataDir(const QString &newDir = "");
    EXTRACHAIN_EXPORT qint64  diskAvailableMemory();
    EXTRACHAIN_EXPORT qint64  diskFreeMemory();
    EXTRACHAIN_EXPORT qint64  diskTotalMemory();

    std::optional<uint64_t> read_file_creation_time_ms(const std::filesystem::path &filepath);

    boost::json::value stringToJsonValue(const std::string &value, const std::type_info &target_type);

    EXTRACHAIN_EXPORT std::string str_to_lower(const std::string &str);
    EXTRACHAIN_EXPORT std::string str_to_upper(const std::string &str);
    bool                          is_hex_string(const std::string &str);
    bool                          is_hex_string_lower(const std::string &str);

    QByteArray                       intToByteArray(const int &number, const int &size);
    std::string                      intToStdString(const int &number, const int &size);
    int                              qByteArrayToInt(const QByteArray &number);
    typedef std::vector<std::string> MerkleDataBlocks;

    EXTRACHAIN_EXPORT void rootMerkleHash(std::vector<std::string>      &listHashes,
                                          std::vector<MerkleDataBlocks> &branchesTree,
                                          const bool                     isHahsing,
                                          std::string                   &result);
    EXTRACHAIN_EXPORT std::string rootMerkleHash(std::string &data);
    EXTRACHAIN_EXPORT std::vector<MerkleDataBlocks> splitListIntoPair(std::vector<std::string> &vector,
                                                                      const bool                isHahsing);
    EXTRACHAIN_EXPORT void                          hashingElements(std::vector<std::string> &vector);
    EXTRACHAIN_EXPORT std::string merkleFormula(const std::string &hash1, const std::string &hash2);
    EXTRACHAIN_EXPORT std::string calculate_hash(const std::string &data,
                                                 HashAlgorithm      hash_algorithm = HashAlgorithm::Blake3);

    namespace detail {
        template <typename T>
        void update_hasher(blake3_hasher &hasher, const T &value) {
            if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
                // eInfo("- '{}'", value);
                blake3_hasher_update(&hasher, value.data(), value.size());
            } else if constexpr (std::is_arithmetic_v<T>) {
                auto str = std::to_string(value);
                // eInfo("- '{}'", str);
                blake3_hasher_update(&hasher, str.data(), str.size());
            } else if constexpr (std::is_enum_v<T>) {
                auto str = std::to_string(static_cast<std::underlying_type_t<T>>(value));
                // eInfo("- '{}'", str);
                blake3_hasher_update(&hasher, str.data(), str.size());
            } else if constexpr (magic::is_optional<T>::value) {
                if (value.has_value()) {
                    update_hasher(hasher, value.value());
                }
            } else {
                auto str = magic::detail::to_string(value);
                // eInfo("- '{}'", str);
                blake3_hasher_update(&hasher, str.data(), str.size());
            }
        }
    } // namespace detail

    template <typename T>
    std::string calculate_hash_blake3(const T &value) {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        if constexpr (boost::describe::has_describe_members<T>::value) {
            boost::mp11::mp_for_each<boost::describe::describe_members<T,
                                                                       boost::describe::mod_any_access
                                                                           | boost::describe::mod_inherited>>(
                [&](auto D) {
                    if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                        auto field_name = magic::detail::clean_type_name(D.name);
                        if (field_name != "sign" && field_name != "signature" && field_name != "hash") {
                            detail::update_hasher(hasher, magic::invoke_member(value, D.pointer));
                        }
                    }
                });
        } else {
            detail::update_hasher(hasher, value);
        }

        uint8_t output[BLAKE3_OUT_LEN];
        blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);

        std::string hash;
        for (uint8_t byte : output) {
            hash += fmt::format("{:02x}", byte);
        }
        return hash;
    }

    template <typename T>
    std::string calculate_hash(const T &value, HashAlgorithm hash_algorithm = HashAlgorithm::Blake3) {
        switch (hash_algorithm) {
        case HashAlgorithm::Blake3:
            return calculate_hash_blake3(value);
        default:
            return "";
        }
    }

    /**
     * @brief Error codes for file hashing operations
     */
    enum class FileHashError {
        FileNotFound, ///< File does not exist
        ReadError,    ///< Error reading file data
        HashError,    ///< Error during hash calculation
        AccessError   ///< Permission or access-related errors
    };

    /**
     * @brief Calculate BLAKE3 hash of a file
     * @param path Path to the file
     * @return Expected containing hex string of hash or FileHashError
     * @retval string Hex representation of BLAKE3 hash on success
     * @retval FileHashError::FileNotFound If file doesn't exist
     * @retval FileHashError::ReadError If file reading fails
     * @retval FileHashError::AccessError If file access is denied
     *
     * @details Uses 64KB buffer for file reading and generates BLAKE3 hash
     * of the entire file content. The resulting hash is returned as a
     * hexadecimal string.
     */
    EXTRACHAIN_EXPORT std::expected<std::string, FileHashError> calculate_hash_file(const FsPath &path);

    enum class ContentError {
        ReadError,
        SizeTooLarge,
        EmptyFile,
        InvalidFile,
        EmptyContent,
        WriteError
    };

    /**
     * Reads entire file content into a byte vector
     * @param path File path to read
     * @return Expected vector with file contents or FileError
     */
    EXTRACHAIN_EXPORT std::expected<std::vector<std::uint8_t>, ContentError> read_file_content(const FsPath &path);

    EXTRACHAIN_EXPORT std::expected<void, Utils::ContentError> write_file_content(
        const FsPath                 &path,
        std::span<const std::uint8_t> content);
    EXTRACHAIN_EXPORT std::expected<void, Utils::ContentError> write_file_content(const FsPath      &path,
                                                                                  const std::string &content);
    EXTRACHAIN_EXPORT std::expected<void, Utils::ContentError> write_file_content(const FsPath &path,
                                                                                  std::string &&content);
    template <std::size_t N>
    EXTRACHAIN_EXPORT std::expected<void, Utils::ContentError> write_file_content(const FsPath &path,
                                                                                  const char (&content)[N]);

    std::string to_hex_impl(const unsigned char *data, size_t size);

    template <typename T>
    std::string to_hex(const std::vector<T> &data) {
        static_assert(std::is_same_v<T, unsigned char> || std::is_same_v<T, uint8_t>,
                      "T must be unsigned char or uint8_t");
        return to_hex_impl(reinterpret_cast<const unsigned char *>(data.data()), data.size());
    }

    std::string to_hex(const std::string &data);
    std::string from_hex(const std::string &data);

    std::string generate_random_hex(size_t length);

    template <size_t N>
    std::array<size_t, N> random_indices(size_t max_index) {
        std::array<size_t, N> indices {};

        if (max_index == 0) {
            return indices;
        }

        const size_t actual_size = std::min(N, max_index);

        std::vector<size_t> all_indices(max_index);
        std::iota(all_indices.begin(), all_indices.end(), 0);

        std::random_device rd;
        std::mt19937       gen(rd());
        std::shuffle(all_indices.begin(), all_indices.end(), gen);

        for (size_t i = 0; i < actual_size; ++i) {
            indices[i] = all_indices[i];
        }

        return indices;
    }

    template <typename T>
    concept Container = std::ranges::range<T>;

    /**
     * @brief Checks if all elements in a container equal the given value
     *
     * @tparam C Container type that satisfies the Container concept
     * @param container The container to check
     * @param value The value to compare against
     * @return true if all elements equal the given value
     * @return false otherwise
     *
     * @note This function is constexpr and can be evaluated at compile-time
     * @see isAllZeros for a specialized version checking for zeros
     */
    template <Container C>
    constexpr bool is_container_value(const C &container, const std::ranges::range_value_t<C> &value) {
        return std::ranges::all_of(container, [&value](const auto &x) {
            return x == value;
        });
    }

    /**
     * @brief Specialized function to check if all elements in a container are zero
     *
     * @tparam C Container type that satisfies the Container concept
     * @param container The container to check
     * @return true if all elements are zero
     * @return false otherwise
     *
     * @requires The container's value type must be arithmetic (integer or floating-point)
     * @note This function is constexpr and can be evaluated at compile-time
     * @see isAllValue for a more general version that can check against any value
     */
    template <Container C>
        requires std::is_arithmetic_v<std::ranges::range_value_t<C>>
    constexpr bool is_container_empty(const C &container) {
        return is_container_value(container, std::ranges::range_value_t<C> { '\0' });
    }

    QString fileMimeType(const QString &filePath);
    QString fileMimeSuffix(const QString &filePath);

    std::vector<std::string> split(const std::string &s, char c);

    /**
     * @brief Remove data and cache files
     */
    EXTRACHAIN_EXPORT void wipeDataFiles();

    EXTRACHAIN_EXPORT QString              detectCompiler();
    EXTRACHAIN_EXPORT QNetworkAddressEntry findLocalIp(PrintDebug debug = PrintDebug::Off);
    EXTRACHAIN_EXPORT QString fixFileName(const QString &fileName, const QString &replaceSymbol = "_");
    EXTRACHAIN_EXPORT bool    isValidIp(const QString &ip);

    EXTRACHAIN_EXPORT bool is_valid_domain(const std::string_view domain);
    EXTRACHAIN_EXPORT bool is_valid_ip(const std::string_view ip);
    EXTRACHAIN_EXPORT bool is_external_ip(const QString &ip);
    EXTRACHAIN_EXPORT bool is_external_ip(const std::string &ip);

    EXTRACHAIN_EXPORT void benchmark(std::function<void(void)> func, int count = 1000);

    enum class FileError {
        InvalidInput,
        OpenError,
        ReadError,
        WriteError,
        SeekError,
        FileTooLarge,
        MappingError
    };

    // Read N bytes from file starting from offset
    EXTRACHAIN_EXPORT std::expected<std::string, FileError> read_file_chunk(const FsPath &file_path,
                                                                            std::uint64_t offset,
                                                                            std::uint64_t size);

    // Write data to file at specific offset
    EXTRACHAIN_EXPORT std::expected<void, FileError> write_file_chunk(const FsPath          &file_path,
                                                                      const std::string_view data,
                                                                      std::uint64_t          offset);

    EXTRACHAIN_EXPORT ExtraChainSettings read_settings();
    EXTRACHAIN_EXPORT bool               write_settings(const ExtraChainSettings &settings);

    /**
     * @enum VersionCompareResult
     * @brief Result of version comparison
     */
    enum class VersionCompareResult {
        Newer, ///< Latest version is newer than current version
        Older, ///< Latest version is older than current version
        Same   ///< Versions are identical
    };

    /**
     * @brief Compares two version strings using semantic versioning rules
     *
     * This function compares version strings in format X.Y.Z or X.Y.Z.W, where:
     * - First three components (X.Y.Z) have priority in comparison
     * - Fourth component (W) is only compared if the first three are identical
     * - Each component is compared numerically, not lexicographically (e.g., 10 > 9)
     * - Missing components are treated as zeros (e.g., "1.2" is equivalent to "1.2.0")
     *
     * @param current The current version string
     * @param latest The latest version string to compare against
     * @return VersionCompareResult indicating if latest version is newer, older, or the same
     *
     * @code
     * // Example usage:
     * auto result = compare_versions("0.20.0", "0.20.1");
     * if (result == VersionCompareResult::NewerVersion) {
     *     // Perform update
     * }
     * @endcode
     */
    EXTRACHAIN_EXPORT VersionCompareResult compare_versions(const std::string &current, const std::string &latest);

    /**
     * @brief Helper function that checks if the latest version is newer than current
     *
     * @param current The current version string
     * @param latest The latest version string to compare against
     * @return bool True if latest version is newer, false otherwise
     */
    EXTRACHAIN_EXPORT bool is_newer_version(const std::string &current, const std::string &latest);

    enum class TimeParseError {
        InvalidFormat,
        EmptyString,
        InvalidUnit,
        InvalidNumber,
        Overflow
    };

    // format: "2d5h3m30s"
    EXTRACHAIN_EXPORT std::expected<std::uint64_t, TimeParseError> parse_time_string(const std::string &time_str);
} // namespace Utils

namespace ChainConst {
    // Main dag folder
    static const int ACTOR_SIZE = 40;

    // Temporary folder
    static const std::string TMP_FOLDER = "tmp";

    // Actors
    static const std::string ACTORS_FOLDER = "actors";

    // Folder with blocks
    static const std::string DAG_FOLDER     = "dag";
    static const std::string DAG_RANGE      = "range";
    static const std::string DAG_RANGE_PATH = DAG_FOLDER + "/" + DAG_RANGE;

    // Cache
    static const std::string DAG_CACHE_FOLDER  = DAG_FOLDER + "/cache";
    static const std::string TRANSACTION_CACHE = DAG_CACHE_FOLDER + "/SelfTransactions.db";
    static const std::string BALANCE_CACHE     = DAG_CACHE_FOLDER + "/BalanceCache.db";

    // Dfs
    static const int DATA_OFFSET = 512;

    enum class DataRowType {
        Universal,
    };

    static const auto MAX_TOKEN_COUNT = BigNumberFloat("1000000000000", NumeralBase::Dec);
} // namespace ChainConst
MSGPACK_ADD_ENUM(ChainConst::DataRowType)

namespace Profiles {
    // To store user private/public keys
    static const std::string folder   = "profiles";
    static const std::string format   = ".profile";
    static const std::string profiles = "profiles";
    // static const std::string encrypt  = "encrypt";
} // namespace Profiles

namespace SearchEnum {
    enum class BlockParam {
        Id = 0,
        Data,
        Hash,
        Null
    };

    enum class TxParam {
        UserSender = 0,
        UserReceiver,
        UserSenderOrReceiver,
        UserSenderOrReceiverOrToken,
        User, // sender or receiver
        Hash,
        Data,
        Null
    };
} // namespace SearchEnum

struct EXTRACHAIN_EXPORT Notification {
    enum NotifyType {
        Deposit,
        Withdrawal,
        Reward,
        Message,
        // TxToUser,
        // TxToMe,
        // ChatMsg,
        // ChatInvite,
        // NewPost,
        // NewEvent,
        // NewFollower
        Unknown = 50
    };

    static NotifyType fromInt(int value) {
        switch (value) {
        case 0:
            return Deposit;
        case 1:
            return Withdrawal;
        case 2:
            return Reward;
        case 3:
            return Message;
        case 50:
            break;
        }
        return Unknown;
    }

    static int toInt(NotifyType value) {
        switch (value) {
        case NotifyType::Deposit:
            return 0;
        case NotifyType::Withdrawal:
            return 1;
        case NotifyType::Reward:
            return 2;
        case Message:
            return 3;
            break;
        case Unknown:
            break;
        }
        return 50;
    }

    std::uint64_t time;
    NotifyType    type;
    QByteArray    data = "";
};

struct EXTRACHAIN_EXPORT StatusTrx {
    enum StatusTrxType {
        None = -1,
        Approved,
        Processing,
        Failed
    };

    static StatusTrxType fromInt(int value) {
        switch (value) {
        case 0:
            return Approved;
        case 1:
            return Processing;
        case 2:
            return Failed;
        }
        return None;
    }

    static int toInt(StatusTrxType value) {
        switch (value) {
        case Approved:
            return 0;
        case Processing:
            return 1;
        case Failed:
            return 2;
        case None:
            return -1;
        }
        return -1;
    }

    static std::string toString(int value) {
        switch (value) {
        case 0:
            return "Approved";
        case 1:
            return "Processing";
        case 2:
            return "Failed";
        case None:
            return "-1";
        }
        return "";
    }
};

#define TIMER_START(name)                                                                                         \
    QElapsedTimer name;                                                                                           \
    name.start();
#define TIMER_END(name) eInfo("{} ms for timer {}", name.elapsed(), #name);
