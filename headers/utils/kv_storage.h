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

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <lmdb.h>

namespace Utils {

enum class KvError {
    Ok,
    OpenFailed,
    TransactionFailed,
    NotFound,
    WriteFailed,
    ReadFailed,
    DeleteFailed,
    CursorFailed,
    MapFull,
    Unknown
};

struct KvConfig {
    std::string path;
    size_t      map_size  = 1ULL * 1024 * 1024 * 1024; // 1GB
    bool        read_only = false;
    bool        create    = true;
};

class KvStorage {
public:
    KvStorage() = default;
    ~KvStorage();

    KvStorage(const KvStorage&)            = delete;
    KvStorage& operator=(const KvStorage&) = delete;
    KvStorage(KvStorage&&) noexcept;
    KvStorage& operator=(KvStorage&&) noexcept;

    std::expected<void, KvError> open(const KvConfig& config);
    void                         close();
    bool                         is_open() const;

    std::expected<void, KvError>        put(const std::string& key, const std::string& value);
    std::expected<std::string, KvError> get(const std::string& key);
    std::expected<void, KvError>        del(const std::string& key);
    bool                                exists(const std::string& key);

    using RangeResult = std::vector<std::pair<std::string, std::string>>;
    std::expected<RangeResult, KvError> range(const std::string& from, const std::string& to);

    std::expected<size_t, KvError> count();

private:
    MDB_env* env_        = nullptr;
    MDB_dbi  dbi_        = 0;
    bool     dbi_opened_ = false;

    KvError map_error(int rc);
};

} // namespace Utils
