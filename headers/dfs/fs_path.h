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
#include <expected>

#ifdef _WIN32
    #include <windows.h>
#endif

namespace Utils {

enum class ConversionError {
    Utf8ToUtf16Failed,
    Utf16ToUtf8Failed,
};

#ifdef _WIN32
[[nodiscard]] inline std::expected<std::wstring, ConversionError> utf8_to_utf16(std::string_view utf8) {
    if (utf8.empty())
        return std::wstring();

    int required_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8.data(),
        static_cast<int>(utf8.size()),
        nullptr,
        0);

    if (required_size <= 0)
        return std::unexpected(ConversionError::Utf8ToUtf16Failed);

    std::wstring wide_str(required_size, L'\0');
    if (!MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            utf8.data(),
            static_cast<int>(utf8.size()),
            wide_str.data(),
            required_size)) {
        return std::unexpected(ConversionError::Utf8ToUtf16Failed);
    }

    return wide_str;
}

[[nodiscard]] inline std::expected<std::string, ConversionError> utf16_to_utf8(const std::wstring& utf16) {
    if (utf16.empty())
        return std::string();

    int required_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        utf16.c_str(),
        static_cast<int>(utf16.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (required_size <= 0)
        return std::unexpected(ConversionError::Utf16ToUtf8Failed);

    std::string utf8_str(required_size, '\0');
    if (!WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            utf16.c_str(),
            static_cast<int>(utf16.size()),
            utf8_str.data(),
            required_size,
            nullptr,
            nullptr)) {
        return std::unexpected(ConversionError::Utf16ToUtf8Failed);
    }

    return utf8_str;
}
#endif

[[nodiscard]] inline std::string normalize_separators(std::string_view path) {
    std::string normalized { path };
#ifdef _WIN32
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
#else
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
#endif
    return normalized;
}

} // namespace Utils

#include <filesystem>
#include <string>
#include <expected>
#include <chrono>
#include "utils/exc_logs.h"
#include "dfs/name_validator.h"

enum class FsError {
    ConversionFailed,
    InvalidPath,
    ValidationError,
    DirectoryTraversalError,
    IoError,
    AccessDenied,
    SymlinkFound,
    ParentNotFound,
    ParentNotDirectory
};

class DirectoryIterator;

class FsPath {
public:
    using TimePoint = std::chrono::system_clock::time_point;

    [[nodiscard]] static std::expected<FsPath, FsError> create(std::string_view utf8_path) {
        auto normalized        = Utils::normalize_separators(utf8_path);
        auto validation_result = PathValidator::validate(normalized);
        if (!validation_result)
            return std::unexpected(FsError::ValidationError);

        try {
#ifdef _WIN32
            auto wide_path = Utils::utf8_to_utf16(normalized);
            if (!wide_path)
                return std::unexpected(FsError::ConversionFailed);
            auto fs_path = std::filesystem::path(*wide_path).lexically_normal();
#else
            auto fs_path = std::filesystem::path(normalized).lexically_normal();
#endif
            // Check symlinks along the path
            auto current = fs_path;
            while (!current.empty()) {
                if (std::filesystem::is_symlink(current)) {
                    return std::unexpected(FsError::SymlinkFound);
                }
                current = current.parent_path();
            }

            // Check parent directory
            auto parent = fs_path.parent_path();
            if (!parent.empty()) {
                if (!std::filesystem::exists(parent)) {
                    return std::unexpected(FsError::ParentNotFound);
                }
                if (!std::filesystem::is_directory(parent)) {
                    return std::unexpected(FsError::ParentNotDirectory);
                }
            }

            return FsPath(std::filesystem::weakly_canonical(fs_path));
        } catch (const std::exception& e) {
            eCritical("Failed to process path: {}", e.what());
            return std::unexpected(FsError::ConversionFailed);
        }
    }

    [[nodiscard]] std::expected<std::string, FsError> string() const {
        try {
#ifdef _WIN32
            auto utf8_result = Utils::utf16_to_utf8(m_path.wstring());
            if (!utf8_result)
                return std::unexpected(FsError::ConversionFailed);
            return *utf8_result;
#else
            return m_path.string();
#endif
        } catch (const std::exception& e) {
            eCritical("Failed to convert path to string: {}", e.what());
            return std::unexpected(FsError::ConversionFailed);
        }
    }

    [[nodiscard]] std::expected<FsPath, FsError> absolute() const {
        try {
            return FsPath(std::filesystem::absolute(m_path));
        } catch (const std::exception& e) {
            eCritical("Failed to get absolute path: {}", e.what());
            return std::unexpected(FsError::InvalidPath);
        }
    }

    [[nodiscard]] std::expected<bool, FsError> exists() const {
        try {
            return std::filesystem::exists(m_path);
        } catch (const std::exception& e) {
            eCritical("Failed to check path existence: {}", e.what());
            return std::unexpected(FsError::IoError);
        }
    }

    [[nodiscard]] std::expected<bool, FsError> is_directory() const {
        try {
            return std::filesystem::is_directory(m_path);
        } catch (const std::exception& e) {
            eCritical("Failed to check if path is directory: {}", e.what());
            return std::unexpected(FsError::IoError);
        }
    }

    [[nodiscard]] std::expected<bool, FsError> is_regular_file() const {
        try {
            return std::filesystem::is_regular_file(m_path);
        } catch (const std::exception& e) {
            eCritical("Failed to check if path is file: {}", e.what());
            return std::unexpected(FsError::IoError);
        }
    }

    [[nodiscard]] std::expected<bool, FsError> is_file_locked() const {
        if (!is_regular_file())
            return std::unexpected(FsError::InvalidPath);

        try {
#ifdef _WIN32
            HANDLE file_handle = CreateFileW(
                m_path.wstring().c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0, // No share
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL);

            if (file_handle == INVALID_HANDLE_VALUE) {
                return GetLastError() == ERROR_SHARING_VIOLATION;
            }
            CloseHandle(file_handle);
            return false;
#else
            struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };

            int fd = open(m_path.c_str(), O_RDWR);
            if (fd == -1)
                return errno == EACCES || errno == EAGAIN;

            int result = fcntl(fd, F_GETLK, &lock);
            close(fd);

            if (result == -1)
                return std::unexpected(FsError::IoError);
            return lock.l_type != F_UNLCK;
#endif
        } catch (const std::exception& e) {
            eCritical("Failed to check file lock status: {}", e.what());
            return std::unexpected(FsError::IoError);
        }
    }

    [[nodiscard]] std::expected<FsPath, FsError> parent_path() const {
        try {
            return FsPath(m_path.parent_path());
        } catch (const std::exception& e) {
            eCritical("Failed to get parent path: {}", e.what());
            return std::unexpected(FsError::InvalidPath);
        }
    }

    [[nodiscard]] std::expected<std::string, FsError> filename() const {
        try {
#ifdef _WIN32
            auto utf8_result = Utils::utf16_to_utf8(m_path.filename().wstring());
            if (!utf8_result)
                return std::unexpected(FsError::ConversionFailed);
            return *utf8_result;
#else
            return m_path.filename().string();
#endif
        } catch (const std::exception& e) {
            eCritical("Failed to get filename: {}", e.what());
            return std::unexpected(FsError::ConversionFailed);
        }
    }

    [[nodiscard]] std::expected<std::string, FsError> extension() const {
        try {
#ifdef _WIN32
            auto utf8_result = Utils::utf16_to_utf8(m_path.extension().wstring());
            if (!utf8_result)
                return std::unexpected(FsError::ConversionFailed);
            return *utf8_result;
#else
            return m_path.extension().string();
#endif
        } catch (const std::exception& e) {
            eCritical("Failed to get extension: {}", e.what());
            return std::unexpected(FsError::ConversionFailed);
        }
    }

    [[nodiscard]] std::expected<std::uintmax_t, FsError> file_size() const {
        try {
            return std::filesystem::file_size(m_path);
        } catch (const std::exception& e) {
            eCritical("Failed to get file size: {}", e.what());
            return std::unexpected(FsError::IoError);
        }
    }

    [[nodiscard]] std::expected<std::uintmax_t, FsError> directory_size() const {
        try {
            std::uintmax_t size = 0;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(m_path)) {
                if (entry.is_regular_file()) {
                    size += std::filesystem::file_size(entry);
                }
            }
            return size;
        } catch (const std::exception& e) {
            eCritical("Failed to calculate directory size: {}", e.what());
            return std::unexpected(FsError::IoError);
        }
    }

    [[nodiscard]] std::expected<TimePoint, FsError> last_write_time() const {
        try {
            auto ftime = std::filesystem::last_write_time(m_path);
            return std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        } catch (const std::exception& e) {
            eCritical("Failed to get last write time: {}", e.what());
            return std::unexpected(FsError::IoError);
        }
    }

    [[nodiscard]] std::expected<bool, FsError> has_read_permission() const {
        try {
            const auto perms = std::filesystem::status(m_path).permissions();
            return (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
        } catch (const std::exception& e) {
            eCritical("Failed to check read permissions: {}", e.what());
            return std::unexpected(FsError::AccessDenied);
        }
    }

    [[nodiscard]] std::expected<DirectoryIterator, FsError> begin() const;
    [[nodiscard]] std::expected<DirectoryIterator, FsError> end() const;

    FsPath& operator/=(const FsPath& other) {
        m_path /= other.m_path;
        return *this;
    }

    friend FsPath operator/(const FsPath& lhs, const FsPath& rhs) {
        return FsPath(lhs) /= rhs;
    }

    bool operator==(const FsPath& other) const {
        return m_path == other.m_path;
    }

    bool operator!=(const FsPath& other) const {
        return !(*this == other);
    }

    [[nodiscard]] const std::filesystem::path& native() const {
        return m_path;
    }

private:
    explicit FsPath(const std::filesystem::path& path)
        : m_path(path) {
    }
    std::filesystem::path m_path;

    friend class DirectoryIterator;
};

class DirectoryIterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = FsPath;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const FsPath*;
    using reference         = const FsPath&;

    DirectoryIterator() = default;
    explicit DirectoryIterator(const std::filesystem::directory_iterator& it)
        : m_it(it) {
        update_current();
    }

    reference operator*() const {
        return m_current;
    }
    pointer operator->() const {
        return &m_current;
    }

    DirectoryIterator& operator++() {
        ++m_it;
        update_current();
        return *this;
    }

    DirectoryIterator operator++(int) {
        DirectoryIterator tmp = *this;
        ++(*this);
        return tmp;
    }

    bool operator==(const DirectoryIterator& other) const {
        return m_it == other.m_it;
    }
    bool operator!=(const DirectoryIterator& other) const {
        return !(*this == other);
    }

private:
    void update_current() {
        if (m_it != std::filesystem::directory_iterator {}) {
            m_current = FsPath(m_it->path());
        }
    }

    std::filesystem::directory_iterator m_it;
    FsPath                              m_current { std::filesystem::path {} };
    friend class FsPath;
};

inline std::expected<DirectoryIterator, FsError> FsPath::begin() const {
    try {
        return DirectoryIterator(std::filesystem::directory_iterator(m_path));
    } catch (const std::exception& e) {
        eCritical("Failed to create directory iterator: {}", e.what());
        return std::unexpected(FsError::DirectoryTraversalError);
    }
}

inline std::expected<DirectoryIterator, FsError> FsPath::end() const {
    return DirectoryIterator();
}
