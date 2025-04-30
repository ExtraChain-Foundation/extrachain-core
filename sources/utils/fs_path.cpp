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

#include "utils/fs_path.h"
#include <boost/filesystem/operations.hpp>

#include "utils/exc_logs.h"
#include "dfs/name_validator.h"

#ifdef _WIN32
    #include <windows.h>
    #include <fcntl.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
    #include <errno.h>
#endif

FsPath::FsPath(const std::filesystem::path& path)
    : m_path(path) {
}

std::expected<std::string, FsError> FsPath::validate_path_component(std::string_view component) {
    auto normalized        = Utils::normalize_separators(component);
    auto validation_result = PathValidator::validate(normalized);
    if (!validation_result)
        return std::unexpected(FsError::ValidationError);

#ifdef _WIN32
    auto wide_path = Utils::utf8_to_utf16(normalized);
    if (!wide_path)
        return std::unexpected(FsError::ConversionFailed);
    auto utf8_result = Utils::utf16_to_utf8(wide_path->c_str());
    if (!utf8_result)
        return std::unexpected(FsError::ConversionFailed);
    return *utf8_result;
#else
    return std::string(normalized);
#endif
}

std::expected<FsPath, FsError> FsPath::create(std::string_view utf8_path) {
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

        auto current = fs_path;
#ifndef Q_OS_IOS
        // while (true) {
        //     if (std::filesystem::is_symlink(current)) {
        //         return std::unexpected(FsError::SymlinkFound);
        //     }
        //     if (current.empty() || current.root_path() == current) {
        //         break;
        //     }
        //     current = current.parent_path();
        // }

        // auto parent = fs_path.parent_path();
        // if (!parent.empty()) {
        //     if (!std::filesystem::exists(parent)) {
        //         return std::unexpected(FsError::ParentNotFound);
        //     }
        //     if (!std::filesystem::is_directory(parent)) {
        //         return std::unexpected(FsError::ParentNotDirectory);
        //     }
        // }
#endif

        return FsPath(std::filesystem::weakly_canonical(fs_path));
    } catch (const std::exception& e) {
        eCritical("Failed to process path: {}", e.what());
        return std::unexpected(FsError::ConversionFailed);
    }
}

std::expected<FsPath, FsError> FsPath::create(const std::string& path) {
    return create(std::string_view(path));
}

std::expected<FsPath, FsError> FsPath::create(const std::filesystem::path& path) {
#ifdef _WIN32
    return create(std::string_view(Utils::utf16_to_utf8(path.wstring()).value()));
#else
    return create(std::string_view(path.string()));
#endif
}

std::expected<void, FsError> FsPath::append(std::string_view component) {
    auto validated = validate_path_component(component);
    if (!validated)
        return std::unexpected(validated.error());

    try {
        m_path = (m_path / validated.value()).lexically_normal();
        return {};
    } catch (const std::exception& e) {
        eCritical("Failed to append path component: {}", e.what());
        return std::unexpected(FsError::InvalidPath);
    }
}

std::expected<std::string, FsError> FsPath::string() const {
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

std::expected<FsPath, FsError> FsPath::absolute() const {
    try {
        return FsPath(std::filesystem::absolute(m_path));
    } catch (const std::exception& e) {
        eCritical("Failed to get absolute path: {}", e.what());
        return std::unexpected(FsError::InvalidPath);
    }
}

bool FsPath::exists() const {
    try {
        auto res = std::filesystem::exists(m_path);
        return res;
    } catch (const std::exception& e) {
        // eCritical("Failed to check path existence: {}", e.what());
        return false;
    }
}

std::expected<bool, FsError> FsPath::is_directory() const {
    try {
        return std::filesystem::is_directory(m_path);
    } catch (const std::exception& e) {
        eCritical("Failed to check if path is directory: {}", e.what());
        return std::unexpected(FsError::IoError);
    }
}

std::expected<bool, FsError> FsPath::is_regular_file() const {
    try {
        return std::filesystem::is_regular_file(m_path);
    } catch (const std::exception& e) {
        eCritical("Failed to check if path is file: {}", e.what());
        return std::unexpected(FsError::IoError);
    }
}

std::expected<bool, FsError> FsPath::is_file_locked() const {
    if (!is_regular_file())
        return std::unexpected(FsError::InvalidPath);

    try {
#ifdef _WIN32
        HANDLE file_handle = CreateFileW(m_path.wstring().c_str(),
                                         GENERIC_READ | GENERIC_WRITE,
                                         0,
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

std::expected<FsPath, FsError> FsPath::parent_path() const {
    try {
        return FsPath(m_path.parent_path());
    } catch (const std::exception& e) {
        eCritical("Failed to get parent path: {}", e.what());
        return std::unexpected(FsError::InvalidPath);
    }
}

std::expected<std::string, FsError> FsPath::filename() const {
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

std::expected<std::string, FsError> FsPath::extension() const {
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

std::expected<std::uintmax_t, FsError> FsPath::file_size() const {
    try {
        return std::filesystem::file_size(m_path);
    } catch (const std::exception& e) {
        // eCritical("Failed to get file size: {}", e.what());
        return std::unexpected(FsError::IoError);
    }
}

std::expected<std::uintmax_t, FsError> FsPath::directory_size() const {
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

std::expected<FsPath::TimePoint, FsError> FsPath::last_write_time() const {
    try {
        std::time_t ftime = boost::filesystem::last_write_time(m_path.string());
        return FsPath::TimePoint(boost::chrono::seconds(ftime));
    } catch (const std::exception& e) {
        eCritical("Failed to get last write time: {}", e.what());
        return std::unexpected(FsError::IoError);
    }
}

std::expected<bool, FsError> FsPath::has_read_permission() const {
    try {
        const auto perms = std::filesystem::status(m_path).permissions();
        return (perms & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
    } catch (const std::exception& e) {
        eCritical("Failed to check read permissions: {}", e.what());
        return std::unexpected(FsError::AccessDenied);
    }
}

std::expected<bool, FsError> FsPath::has_write_permission() const {
    try {
        const auto perms = std::filesystem::status(m_path).permissions();
        return (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
    } catch (const std::exception& e) {
        eCritical("Failed to check write permissions: {}", e.what());
        return std::unexpected(FsError::AccessDenied);
    }
}

// FsPath& FsPath::operator/=(const FsPath& other) {
//     m_path /= other.m_path;
//     return *this;
// }

// FsPath operator/(const FsPath& lhs, const FsPath& rhs) {
//     return FsPath(lhs) /= rhs;
// }

bool FsPath::operator==(const FsPath& other) const {
    return m_path == other.m_path;
}

bool FsPath::operator!=(const FsPath& other) const {
    return !(*this == other);
}

const std::filesystem::path& FsPath::native() const {
    return m_path;
}

DirectoryIterator::DirectoryIterator() = default;

DirectoryIterator::DirectoryIterator(const std::filesystem::directory_iterator& it)
    : m_it(it)
    , m_current(std::filesystem::path {}) {
    update_current();
}

void DirectoryIterator::update_current() {
    if (m_it != std::filesystem::directory_iterator {}) {
        m_current = FsPath(m_it->path());
    }
}

DirectoryIterator::reference DirectoryIterator::operator*() const {
    return m_current;
}

DirectoryIterator::pointer DirectoryIterator::operator->() const {
    return &m_current;
}

DirectoryIterator& DirectoryIterator::operator++() {
    ++m_it;
    update_current();
    return *this;
}

DirectoryIterator DirectoryIterator::operator++(int) {
    DirectoryIterator tmp = *this;
    ++(*this);
    return tmp;
}

bool DirectoryIterator::operator==(const DirectoryIterator& other) const {
    return m_it == other.m_it;
}

bool DirectoryIterator::operator!=(const DirectoryIterator& other) const {
    return !(*this == other);
}

std::expected<DirectoryIterator, FsError> FsPath::begin() const {
    try {
        return DirectoryIterator(std::filesystem::directory_iterator(m_path));
    } catch (const std::exception& e) {
        eCritical("Failed to create directory iterator: {}", e.what());
        return std::unexpected(FsError::DirectoryTraversalError);
    }
}

std::expected<DirectoryIterator, FsError> FsPath::end() const {
    return DirectoryIterator();
}

bool FsPath::exists_and_size_not_zero() const {
    bool exists    = this->exists();
    auto file_size = this->file_size();
    if (!file_size.has_value()) {
        return false;
    }
    return exists && file_size.value() > 0;
}
