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

#include "utils/exc_utils.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTcpSocket>

#include <string>
#include <string_view>
#include <random>
#include <limits>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <sodium.h>

// #include "boost/asio.hpp" // need qmake fix
#include "boost/version.hpp"

#include "cpp-base64/base64.cpp"

#include "extrachain_version.h"
#include "encryption/encryption_tools.h"
// #include "managers/data_mining_manager.h"
#include "dfs/dfs_utils.h"

#ifndef EXTRACHAIN_CMAKE
    #include "preconfig.h"
#endif

std::string Utils::calculate_hash(const std::string &data, HashAlgorithm hash_algorithm) {
    switch (hash_algorithm) {
    case HashAlgorithm::Blake3: {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, data.data(), data.size());

        uint8_t hash[BLAKE3_OUT_LEN];
        blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);

        return fmt::format("{:02x}", fmt::join(std::span(hash, BLAKE3_OUT_LEN), ""));
    }
    default:
        eFatal("Unknown hash algorithm");
    }
}

// SERIALIZATION //

std::vector<std::string> Utils::split(const std::string &s, char c) {
    auto end   = s.cend();
    auto start = end;

    std::vector<std::string> v;
    for (auto it = s.cbegin(); it != end; ++it) {
        if (*it != c) {
            if (start == end)
                start = it;
            continue;
        }
        if (start != end) {
            v.emplace_back(start, it);
            start = end;
        }
    }
    if (start != end)
        v.emplace_back(start, end);
    return v;
}

std::string Utils::str_to_lower(const std::string &str) {
    std::string str_ = str;
    std::transform(str_.begin(), str_.end(), str_.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return str_;
}

std::string Utils::str_to_upper(const std::string &str) {
    std::string str_ = str;
    std::transform(str_.begin(), str_.end(), str_.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
    return str_;
}

bool Utils::is_hex_string(const std::string &str) {
    if (str.empty())
        return false;
    size_t start = 0;
    if (str.length() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        start = 2;
    }
    return std::all_of(str.begin() + start, str.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

bool Utils::is_hex_string_lower(const std::string &str) {
    if (str.empty())
        return false;

    size_t start = 0;
    if (str.length() > 2 && str[0] == '0' && str[1] == 'x') {
        start = 2;
    }

    return std::all_of(str.begin() + start, str.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

QByteArray Utils::intToByteArray(const int &number, const int &size) {
    auto num = QByteArray::number(number);
    Q_ASSERT(num.size() <= size);
    auto res = QByteArray(size - num.size(), '0') + num;
    return res;
}

std::string Utils::intToStdString(const int &number, const int &size) {
    auto num = std::to_string(number);
    Q_ASSERT(num.size() <= size);
    auto res = std::string(size - num.size(), '0') + num;
    return res;
}

int Utils::qByteArrayToInt(const QByteArray &number) {
    QByteArray num = "";
    int        i   = 0;
    //    bool flag = false;
    while (i < number.size()) {
        if (number[i] == '0')
            i++;
        else
            break;
    }
    while (i < number.size()) {
        num += number[i];
        i++;
    }
    int res = num.toInt();
    return res;
}

void Utils::rootMerkleHash(std::vector<std::string>      &listHashes,
                           std::vector<MerkleDataBlocks> &branchesTree,
                           const bool                     isHahsing,
                           std::string                   &result) {
    if (listHashes.empty()) {
        eFatal("Root merkle hash: list is empty");
    };
    const auto       splittedList = splitListIntoPair(listHashes, isHahsing);
    MerkleDataBlocks merkleBlocks;

    for (int index = 0; index < splittedList.size(); index++) {
        const auto pair = splittedList[index];

        if (pair.size() == 1) {
            merkleBlocks.push_back(pair[0]);
        } else {
            merkleBlocks.push_back(merkleFormula(pair[0], pair[1]));
        }
    }
    branchesTree.push_back(merkleBlocks);

    if (merkleBlocks.size() != 1) {
        rootMerkleHash(merkleBlocks, branchesTree, false, result);
    } else {
        result = branchesTree[branchesTree.size() - 1][0];
    }
}

std::string Utils::rootMerkleHash(std::string &data) {
    std::string                   result;
    std::vector<MerkleDataBlocks> branches;
    std::vector<std::string>      dataList;
    dataList.push_back(data);
    rootMerkleHash(dataList, branches, true, result);
    return result;
}

std::vector<Utils::MerkleDataBlocks> Utils::splitListIntoPair(std::vector<std::string> &vector,
                                                              const bool                isHahsing) {
    std::vector<MerkleDataBlocks> result;

    if (vector.empty())
        return result;

    if (isHahsing)
        hashingElements(vector);

    int        position     = 0;
    int        step         = 2;
    const int  sizeVector   = vector.size();
    bool       isLastPair   = sizeVector <= 2;
    const bool isPairVector = (sizeVector % 2 == 0) ? true : false;
    const int  next         = 1;

    while (position < sizeVector) {
        std::vector<std::string> pair;
        if (isLastPair) {
            pair.push_back(vector[position]);
            if (isLastPair)
                pair.push_back(vector[position + next]);
        } else {
            pair.push_back(vector[position]);
            pair.push_back(vector[position + next]);
        }

        if (!isPairVector) {
            position += ((position + step) > sizeVector) ? 1 : 2;
            isLastPair = ((sizeVector - 1) - position) < 1;
        } else {
            position += step;
            isLastPair = (sizeVector - position) < 2;
        }

        result.push_back(pair);
    }
    return result;
}

void Utils::hashingElements(std::vector<std::string> &vector) {
    for (int i = 0; i < vector.size(); i++) {
        vector[i] = Utils::calculate_hash(vector[i]);
    }
}

std::string Utils::merkleFormula(const std::string &hash1, const std::string &hash2) {
    return Utils::calculate_hash(hash1 + hash2);
}

std::expected<std::string, Utils::FileHashError> Utils::calculate_hash_file(const FsPath &path) {
    auto exists_result = path.exists();
    if (!exists_result) {
        return std::unexpected(FileHashError::FileNotFound);
    }

    auto has_read = path.has_read_permission();
    if (!has_read) {
        return std::unexpected(FileHashError::AccessError);
    }
    if (!*has_read) {
        return std::unexpected(FileHashError::AccessError);
    }

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    constexpr size_t     BUFFER_SIZE = 64 * 1024;
    std::vector<uint8_t> buffer(BUFFER_SIZE);

    FILE *file = fopen(path.native().string().c_str(), "rb");
    if (!file) {
        return std::unexpected(FileHashError::ReadError);
    }

    size_t bytes_read;
    while ((bytes_read = fread(buffer.data(), 1, buffer.size(), file)) > 0) {
        blake3_hasher_update(&hasher, buffer.data(), bytes_read);
        if (ferror(file)) {
            fclose(file);
            return std::unexpected(FileHashError::ReadError);
        }
    }

    fclose(file);

    uint8_t hash[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);

    auto result = fmt::format("{:02x}", fmt::join(std::span(hash, BLAKE3_OUT_LEN), ""));
    return result;
}

QString Utils::fileMimeType(const QString &filePath) {
    QMimeDatabase db;
    QMimeType     type = db.mimeTypeForFile(filePath);
    return type.name();
}

QString Utils::fileMimeSuffix(const QString &filePath) {
    QMimeDatabase db;
    QMimeType     type = db.mimeTypeForFile(filePath);
    return type.preferredSuffix();
}

std::string Serialization::serialize(const std::vector<std::string> &list) {
    std::string              res;
    std::vector<std::string> reslist;
    for (int i = 0; i < list.size(); i++) {
        reslist.push_back(Utils::to_base64(list.at(i)));
    }
    res = boost::algorithm::join(reslist, "|");
    return res;
}

std::vector<std::string> Serialization::deserialize(const std::string &serialized) {
    std::vector<std::string> templist;
    std::vector<std::string> reslist;
    boost::algorithm::split(templist, serialized, boost::algorithm::is_any_of("|"));
    if (templist.empty()) {
        eLog("deserialize error: empty list after split");
    }
    for (int i = 0; i < templist.size(); i++) {
        auto decoded = Utils::from_base64(templist.at(i));
        if (!decoded.has_value()) {
            eFatal("Incorrect Serialization::deserialize");
        }
        reslist.push_back(decoded.value());
    }
    return reslist;
}

void Utils::wipeDataFiles() {
    // QString current = QDir::currentPath();

    QDir("blockchain").removeRecursively();
    QDir(QString::fromStdString(DfsB::fsActrRoot)).removeRecursively();
    QDir("keystore").removeRecursively();
    QDir("tmp").removeRecursively();
    QDir("encrypt").removeRecursively();
    QDir("tokens").removeRecursively();
    QFile(".settings").remove();
    QFile(".auth_hash").remove();

    // QDir dir(QDir::currentPath());
    // dir.cdUp();
    // QDir::setCurrent(dir.canonicalPath());
    // QString dataName = Utils::dataDir();
    // QDir(dataName).removeRecursively();
    // QDir().mkpath(dataName);

    // QString shareFolder = QStandardPaths::standardLocations(QStandardPaths::AppDataLocation).value(0) +
    // "/Share"; QDir(shareFolder).removeRecursively();

    // QDir::setCurrent(current);
}

qint64 Utils::diskFreeMemory() {
    QStorageInfo x(qApp->applicationDirPath());
    return x.bytesFree();
}

qint64 Utils::diskTotalMemory() {
    QStorageInfo x(qApp->applicationDirPath());
    return x.bytesTotal();
}

QString Utils::dataDir(const QString &newDir) {
    static QString current = "extrachain-data";

    if (!newDir.isEmpty())
        current = Utils::fixFileName(newDir);

    return current;
}

std::string Utils::to_hex(std::vector<unsigned char> &data) {
    size_t            psize = data.size() * 2 + 1;
    std::vector<char> p(psize);
    sodium_bin2hex(p.data(), psize, data.data(), data.size());
    std::string s(p.begin(), p.end());
    // s.erase(--s.end());
    return s;
}

std::string Utils::to_hex(const std::string &data) {
    std::vector<unsigned char> v(data.begin(), data.end());
    return to_hex(v);
}

std::string Utils::from_hex(const std::string &data) {
    std::vector<unsigned char> p;
    p.resize(data.length() / 2 + 1);
    const char *end;
    size_t      size;
    int         r = sodium_hex2bin(p.data(), p.size(), data.c_str(), data.length(), NULL, &size, &end);
    std::string res;
    if (r == 0) {
        res = std::string(p.begin(), p.end());
        res.resize(res.size() - 1);
    }
    return res;
}

std::string Utils::generate_random_hex(size_t length) {
    std::random_device              rd;
    std::mt19937                    gen(rd());
    std::uniform_int_distribution<> dis(0, 15);

    std::string result;
    result.reserve(length);

    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < length; i++) {
        result += hex[dis(gen)];
    }

    return result;
}

QString Utils::detectCompiler() {
#ifdef __clang__
    #if __clang_major__ < 11
        #error "Clang must be version 11 or higher"
    #endif
#elif __GNUC__
    #if __GNUC__ < 12
        #error "GCC must be version 12 or higher"
    #endif
#elif _MSC_VER && !__INTEL_COMPILER
#else
    #error "Compiler not supported"
#endif

#if __GNUC__ > 4
    QString gcc = "GCC";
    #ifdef __MINGW32__
    gcc = "MinGW";
    #endif
    return QString("%4 %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__).arg(gcc);
#endif

#if _MSC_VER && !__INTEL_COMPILER
    QString msvcVersion;
    msvcVersion = "MSVC " + QString::number(_MSC_FULL_VER);
    msvcVersion.insert(7, ".");
    msvcVersion.insert(10, ".");
#endif

#ifdef __clang__
    QString compiler =
        QString("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
    #if __APPLE__
    compiler.prepend("Apple ");
    #endif
    #if _MSC_VER && !__INTEL_COMPILER
    compiler += " (" + msvcVersion + ")";
    #endif
    return compiler;
#endif

#if _MSC_VER && !__INTEL_COMPILER
    return msvcVersion;
#else
    return "unknown";
#endif
}

QNetworkAddressEntry Utils::findLocalIp(PrintDebug debug) {
    const auto                allInterfaces = QNetworkInterface::allInterfaces();
    const QHostAddress       &localhost     = QHostAddress(QHostAddress::LocalHost);
    std::vector<QHostAddress> localIpNotConnect;

    for (const QNetworkInterface &networkInterface : allInterfaces) {
        const auto entries = networkInterface.addressEntries();

        for (const QNetworkAddressEntry &address : entries) {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol && address.ip() != localhost) {
                if (debug == PrintDebug::On) {
                    eLog("[FindLocalIp] Find local ip candidate: {}", networkInterface);
                }

                localIpNotConnect.push_back(address.ip());
            }
        }
    }

    for (const QNetworkInterface &networkInterface : allInterfaces) {
        const auto entries = networkInterface.addressEntries();

        for (const QNetworkAddressEntry &entry : entries) {
            const auto flags = networkInterface.flags();

            bool isLoopBack     = flags.testFlag(QNetworkInterface::IsLoopBack);
            bool isPointToPoint = flags.testFlag(QNetworkInterface::IsPointToPoint);
            bool isRunning      = flags.testFlag(QNetworkInterface::IsRunning);
            if (!isRunning || !networkInterface.isValid() || isLoopBack || isPointToPoint)
                continue;

            auto socket = std::make_unique<QTcpSocket>();
            socket->bind(entry.ip());
            socket->connectToHost("8.8.8.8", 53);
            if (!socket->waitForConnected(1000)) {
                socket->connectToHost("1.1.1.1", 53);
                if (!socket->waitForConnected(1000))
                    continue;
            }

            if (Utils::vector_contains(localIpNotConnect, entry.ip())) {
                QString name = networkInterface.name();

                if (name.left(2) == "vm")
                    continue;
                if (name.left(2) == "wl" || name.left(3) == "eth" || name.left(2) == "en"
                    || name.left(8) == "wireless") {
                    return entry;
                }
            }
        }
    }

    eCritical("[Network] Can't find local ip, set 0.0.0.0");
    QNetworkAddressEntry entry;
    entry.setIp(QHostAddress::AnyIPv4);
    return entry;
}

QString Utils::fixFileName(const QString &fileName, const QString &replaceSymbol) {
    QString fixedName = fileName.simplified();
    fixedName         = fixedName.replace(QRegularExpression("[+%@!:*?/\"<>|«»]+"), replaceSymbol);
    fixedName         = fixedName.replace("\\", replaceSymbol);
    return fixedName;
}

bool Utils::isValidIp(const QString &ip) {
    QHostAddress address(ip);
    return QAbstractSocket::IPv4Protocol == address.protocol();
}

void Utils::benchmark(std::function<void()> func, int count) {
    while (true) {
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i != count; i++) {
            func();
        }
        eLog("{} ms", timer.elapsed());
    }
}

std::string Utils::extrachainVersion() {
    return extrachain_version;
}

std::string Utils::sodiumVersion() {
    return sodium_version_string();
}

std::string Utils::boostVersion() {
    int major = BOOST_VERSION / 100000;
    int minor = BOOST_VERSION / 100 % 1000;
    int patch = BOOST_VERSION % 100;
    return fmt::format("{}.{}.{}", major, minor, patch);
}

[[maybe_unused]] std::string Utils::boostAsioVersion() {
    return "";
}

std::string Utils::platformDelimeter() {
#ifdef _WIN32
    char del;
    std::wcstombs(&del, &std::filesystem::path::preferred_separator, 1);
    return std::string(1, del);
#else
    return std::string(1, std::filesystem::path::preferred_separator);
#endif
}

template <typename T>
boost::json::value optionalToJson(const std::optional<T> &opt) {
    if (!opt) {
        return boost::json::value(nullptr);
    }
    return stringToJsonValue(std::to_string(*opt), typeid(T));
}

boost::json::value Utils::stringToJsonValue(const std::string &value, const std::type_info &target_type) {
    if (value.empty()) {
        return boost::json::value(nullptr);
    }

    std::string type_name = boost::core::demangle(target_type.name());

    if (type_name.find("string") != std::string::npos) {
        return boost::json::value(std::string(value));
    }

    try {
        if (type_name.find("int") != std::string::npos || type_name.find("long") != std::string::npos) {
            if (type_name.find("unsigned") != std::string::npos) {
                return boost::json::value(std::stoull(value));
            }
            return boost::json::value(std::stoll(value));
        }

        if (type_name.find("float") != std::string::npos || type_name.find("double") != std::string::npos) {
            return boost::json::value(std::stod(value));
        }

        if (type_name.find("bool") != std::string::npos) {
            return boost::json::value(value == "true" || value == "1");
        }
    } catch (...) {
        return boost::json::value(std::string(value));
    }

    return boost::json::value(std::string(value));
}

std::expected<std::vector<std::uint8_t>, Utils::ContentError> Utils::read_file_content(const FsPath &path) {
    // Get file size
    const auto size = path.file_size();
    if (!size.has_value()) {
        eLog("Failed to get file size: {}", path.string().value_or("invalid path"));
        return std::unexpected(ContentError::ReadError);
    }

    if (*size == 0) {
        eLog("File is empty: {}", path.string().value_or("invalid path"));
        return std::unexpected(ContentError::EmptyFile);
    }

    // Check if file size is reasonable (e.g., less than 4GB)
    constexpr std::uintmax_t MAX_FILE_SIZE = 4ULL * 1024 * 1024 * 1024;
    if (*size > MAX_FILE_SIZE) {
        eLog("File too large: {} bytes", *size);
        return std::unexpected(ContentError::SizeTooLarge);
    }

    // Read file content
    std::vector<uint8_t> content;
    content.reserve(static_cast<size_t>(*size));

    std::ifstream file(path.native(), std::ios::binary);
    if (!file) {
        eLog("Failed to open file: {}", path.string().value_or("invalid path"));
        return std::unexpected(ContentError::ReadError);
    }

    content.insert(content.begin(), std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

    if (file.fail()) {
        eLog("Failed to read file: {}", path.string().value_or("invalid path"));
        return std::unexpected(ContentError::ReadError);
    }

    return content;
}

std::expected<std::string, Utils::FileError> Utils::read_file_chunk(const FsPath &file_path,
                                                                    uint64_t      offset,
                                                                    uint64_t      size) {
    if (size == 0) {
        return std::unexpected(FileError::InvalidInput);
    }

    try {
        auto exists = file_path.exists();
        if (!exists) {
            return std::unexpected(FileError::InvalidInput);
        }

        auto file_size = file_path.file_size();
        if (!file_size.has_value()) {
            return std::unexpected(FileError::InvalidInput);
        }

        if (offset >= file_size.value()) {
            return std::unexpected(FileError::InvalidInput);
        }

        const auto actual_size = std::min(size, file_size.value() - offset);

        std::ifstream file(file_path.native(), std::ios::binary);
        if (!file) {
            return std::unexpected(FileError::OpenError);
        }

        file.seekg(offset);
        if (file.fail()) {
            return std::unexpected(FileError::SeekError);
        }

        std::string result(actual_size, '\0');
        if (!file.read(result.data(), actual_size)) {
            return std::unexpected(FileError::ReadError);
        }

        return result;

    } catch (const std::exception &e) {
        eCritical("Error reading file {}: {}", file_path.native().string(), e.what());
        return std::unexpected(FileError::ReadError);
    }
}
std::expected<void, Utils::FileError> extend_file_size(const std::filesystem::path &path,
                                                       std::uint64_t                current_size,
                                                       std::uint64_t                required_size) {
    using FileError = Utils::FileError;
    if (required_size <= current_size) {
        return {};
    }

    try {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        if (!file) {
            eWarning("Failed to open file for extending: {}", path.string());
            return std::unexpected(FileError::WriteError);
        }

        // Just set the file size
        file.seekp(required_size - 1);
        file.put(0);
        file.flush();

        return {};
    } catch (const std::exception &e) {
        eCritical("Error while extending file: {}", e.what());
        return std::unexpected(FileError::WriteError);
    }
}
// Create or extend file without modifying existing content
std::expected<void, Utils::FileError> prepare_file(const FsPath &file_path, std::uint64_t required_size) {
    using FileError = Utils::FileError;

    try {
        auto exists = file_path.exists();

        if (!exists) {
            // Create new file of required size
            std::ofstream file(file_path.native(), std::ios::binary);
            if (!file) {
                eCritical("Failed to create file {}", file_path.native().string());
                return std::unexpected(FileError::WriteError);
            }
            file.seekp(required_size - 1);
            file.put(0);
            return {};
        }

        // File exists - check if we need to extend
        auto current_size = file_path.file_size();
        if (!current_size.has_value()) {
            return std::unexpected(FileError::InvalidInput);
        }

        if (required_size > current_size.value()) {
            // Extend existing file preserving content
            std::fstream file(file_path.native(), std::ios::binary | std::ios::in | std::ios::out);
            if (!file) {
                eCritical("Failed to open file for extending {}", file_path.native().string());
                return std::unexpected(FileError::WriteError);
            }
            file.seekp(required_size - 1);
            file.put(0);
        }

        return {};
    } catch (const std::exception &e) {
        eCritical("Error preparing file {}: {}", file_path.native().string(), e.what());
        return std::unexpected(FileError::WriteError);
    }
}

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

std::expected<void, Utils::FileError> Utils::write_file_chunk(const FsPath          &file_path,
                                                              const std::string_view data,
                                                              uint64_t               offset) {
    using FileError = Utils::FileError;

    // Check if file path is valid
    const auto path_str = file_path.string();
    if (!path_str.has_value()) {
        eLog("Failed to get string representation of file path");
        return std::unexpected(FileError::InvalidInput);
    }

    // Check if we have write permissions for the file or its parent directory if file doesn't exist
    auto exists = file_path.exists();
    if (exists) {
        // File exists - check write permissions
        // auto parent = file_path.parent_path();
        // if (!parent.has_value()) {
        //     eLog("Failed to get parent path");
        //     return std::unexpected(FileError::OpenError);
        // }
    }

    // Open file in appropriate mode
    std::fstream file;
    file.open(path_str.value(), std::ios::in | std::ios::out | std::ios::binary);

    if (!file.is_open()) {
        // If file doesn't exist, create it
        file.clear();
        file.open(path_str.value(), std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            eLog("Failed to create file: {}", path_str.value());
            return std::unexpected(FileError::OpenError);
        }
        file.close();
        file.open(path_str.value(), std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!file.is_open()) {
        eLog("Failed to open file: {}", path_str.value());
        return std::unexpected(FileError::OpenError);
    }

    // Get current file size
    file.seekg(0, std::ios::end);
    if (file.fail()) {
        eLog("Failed to seek to end of file");
        return std::unexpected(FileError::SeekError);
    }

    const auto file_size = file.tellg();
    if (file_size == -1) {
        eLog("Failed to get file size");
        return std::unexpected(FileError::ReadError);
    }

    // Handle different offset cases
    if (offset > static_cast<uint64_t>(file_size)) {
        // Need to pad with zeros
        file.seekp(file_size, std::ios::beg);
        if (file.fail()) {
            eLog("Failed to seek to file_size position");
            return std::unexpected(FileError::SeekError);
        }

        const std::vector<char> padding(offset - file_size, '\0');
        file.write(padding.data(), padding.size());
        if (file.fail()) {
            eLog("Failed to write padding");
            return std::unexpected(FileError::WriteError);
        }
    }

    // Seek to the target position
    file.seekp(offset, std::ios::beg);
    if (file.fail()) {
        eLog("Failed to seek to target position");
        return std::unexpected(FileError::SeekError);
    }

    // Write the actual data
    file.write(data.data(), data.size());
    if (file.fail()) {
        eLog("Failed to write data");
        return std::unexpected(FileError::WriteError);
    }

    // Ensure all data is written
    file.flush();
    if (file.fail()) {
        eLog("Failed to flush file");
        return std::unexpected(FileError::WriteError);
    }

    return {};
}
