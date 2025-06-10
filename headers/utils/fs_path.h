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

#include <algorithm>
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

        int required_size = MultiByteToWideChar(CP_UTF8,
                                                MB_ERR_INVALID_CHARS,
                                                utf8.data(),
                                                static_cast<int>(utf8.size()),
                                                nullptr,
                                                0);

        if (required_size <= 0)
            return std::unexpected(ConversionError::Utf8ToUtf16Failed);

        std::wstring wide_str(required_size, L'\0');
        if (!MultiByteToWideChar(CP_UTF8,
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

        int required_size = WideCharToMultiByte(CP_UTF8,
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
        if (!WideCharToMultiByte(CP_UTF8,
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
#include <boost/chrono.hpp>
#include <boost/chrono/time_point.hpp>
#include <boost/chrono/chrono_io.hpp>

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

    explicit FsPath() = default;

    [[nodiscard]] static std::expected<FsPath, FsError> create(std::string_view utf8_path);
    static std::expected<FsPath, FsError>               create(const std::string& path);
    static std::expected<FsPath, FsError>               create(const std::filesystem::path& path);

    std::expected<void, FsError> append(std::string_view component);

    [[nodiscard]] std::expected<std::string, FsError>       string() const;
    [[nodiscard]] std::expected<FsPath, FsError>            absolute() const;
    [[nodiscard]] bool                                      exists() const;
    [[nodiscard]] std::expected<bool, FsError>              is_directory() const;
    [[nodiscard]] std::expected<bool, FsError>              is_regular_file() const;
    [[nodiscard]] std::expected<bool, FsError>              is_file_locked() const;
    [[nodiscard]] std::expected<FsPath, FsError>            parent_path() const;
    [[nodiscard]] std::expected<std::string, FsError>       filename() const;
    [[nodiscard]] std::expected<std::string, FsError>       extension() const;
    [[nodiscard]] std::expected<std::uintmax_t, FsError>    file_size() const;
    [[nodiscard]] std::expected<std::uintmax_t, FsError>    directory_size() const;
    [[nodiscard]] std::expected<TimePoint, FsError>         last_write_time() const;
    [[nodiscard]] std::expected<bool, FsError>              has_read_permission() const;
    [[nodiscard]] std::expected<bool, FsError>              has_write_permission() const;
    [[nodiscard]] std::expected<DirectoryIterator, FsError> begin() const;
    [[nodiscard]] std::expected<DirectoryIterator, FsError> end() const;
    [[nodiscard]] bool                                      exists_and_size_not_zero() const;

    FsPath&                                    operator/=(const FsPath& other);
    friend FsPath                              operator/(const FsPath& lhs, const FsPath& rhs);
    bool                                       operator==(const FsPath& other) const;
    bool                                       operator!=(const FsPath& other) const;
    [[nodiscard]] const std::filesystem::path& native() const;

private:
    explicit FsPath(const std::filesystem::path& path);
    static std::expected<std::string, FsError> validate_path_component(std::string_view component);

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

    DirectoryIterator();
    explicit DirectoryIterator(const std::filesystem::directory_iterator& it);

    reference          operator*() const;
    pointer            operator->() const;
    DirectoryIterator& operator++();
    DirectoryIterator  operator++(int);
    bool               operator==(const DirectoryIterator& other) const;
    bool               operator!=(const DirectoryIterator& other) const;

private:
    void                                update_current();
    std::filesystem::directory_iterator m_it;
    FsPath                              m_current { std::filesystem::path {} };
    friend class FsPath;
};
