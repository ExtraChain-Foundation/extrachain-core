/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "utils/file_io.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
    #include <io.h>
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace FileIo {
    namespace {
        std::filesystem::path temporary_path(const std::filesystem::path& path) {
            static std::atomic_uint64_t sequence { 0 };
#ifdef _WIN32
            const auto process_id = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
            const auto process_id = static_cast<std::uint64_t>(::getpid());
#endif
            const auto value = std::chrono::steady_clock::now().time_since_epoch().count()
                               ^ static_cast<std::int64_t>(sequence.fetch_add(1, std::memory_order_relaxed));
            auto temporary = path;
            temporary += ".tmp-" + std::to_string(process_id) + '-' + std::to_string(value);
            return temporary;
        }

        FILE* open_write(const std::filesystem::path& path) {
#ifdef _WIN32
            return _wfopen(path.c_str(), L"wb");
#else
            return std::fopen(path.c_str(), "wb");
#endif
        }

        bool durable_flush(FILE* file) {
            if (std::fflush(file) != 0) {
                return false;
            }
#ifdef _WIN32
            return _commit(_fileno(file)) == 0;
#else
            return ::fsync(fileno(file)) == 0;
#endif
        }

        bool replace(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
            return MoveFileExW(source.c_str(),
                               destination.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
                   != 0;
#else
            std::error_code error;
            std::filesystem::rename(source, destination, error);
            return !error;
#endif
        }

        bool sync_parent_directory(const std::filesystem::path& path) {
#ifdef _WIN32
            static_cast<void>(path);
            return true;
#else
            const auto parent     = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
            const int  descriptor = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
            if (descriptor < 0) {
                return false;
            }
            const bool synced = ::fsync(descriptor) == 0;
            ::close(descriptor);
            return synced;
#endif
        }
    } // namespace

    std::expected<std::string, Error> read_all(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return std::unexpected(Error::OpenFailed);
        }
        std::string data { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        if (!input.eof() && input.fail()) {
            return std::unexpected(Error::ReadFailed);
        }
        return data;
    }

    std::expected<void, Error> write_atomic(const std::filesystem::path& path, std::string_view data) {
        const auto temporary = temporary_path(path);
        FILE*      file      = open_write(temporary);
        if (file == nullptr) {
            return std::unexpected(Error::OpenFailed);
        }

        const auto written = std::fwrite(data.data(), 1, data.size(), file);
        if (written != data.size()) {
            std::fclose(file);
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return std::unexpected(Error::WriteFailed);
        }
        if (!durable_flush(file)) {
            std::fclose(file);
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return std::unexpected(Error::FlushFailed);
        }
        if (std::fclose(file) != 0) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return std::unexpected(Error::FlushFailed);
        }
        if (!replace(temporary, path)) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return std::unexpected(Error::ReplaceFailed);
        }
        if (!sync_parent_directory(path)) {
            return std::unexpected(Error::DirectorySyncFailed);
        }
        return {};
    }

} // namespace FileIo
