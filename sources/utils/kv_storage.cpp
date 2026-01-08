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

#include "utils/kv_storage.h"
#include <filesystem>

namespace Utils {

KvStorage::~KvStorage() {
    close();
}

KvStorage::KvStorage(KvStorage&& other) noexcept
    : env_(other.env_)
    , dbi_(other.dbi_)
    , dbi_opened_(other.dbi_opened_) {
    other.env_        = nullptr;
    other.dbi_        = 0;
    other.dbi_opened_ = false;
}

KvStorage& KvStorage::operator=(KvStorage&& other) noexcept {
    if (this != &other) {
        close();
        env_              = other.env_;
        dbi_              = other.dbi_;
        dbi_opened_       = other.dbi_opened_;
        other.env_        = nullptr;
        other.dbi_        = 0;
        other.dbi_opened_ = false;
    }
    return *this;
}

std::expected<void, KvError> KvStorage::open(const KvConfig& config) {
    if (env_) {
        return std::unexpected(KvError::OpenFailed);
    }

    if (config.create) {
        std::filesystem::create_directories(config.path);
    }

    int rc = mdb_env_create(&env_);
    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    rc = mdb_env_set_mapsize(env_, config.map_size);
    if (rc != 0) {
        mdb_env_close(env_);
        env_ = nullptr;
        return std::unexpected(map_error(rc));
    }

    unsigned int flags = 0;
    if (config.read_only) {
        flags |= MDB_RDONLY;
    }

    rc = mdb_env_open(env_, config.path.c_str(), flags, 0664);
    if (rc != 0) {
        mdb_env_close(env_);
        env_ = nullptr;
        return std::unexpected(map_error(rc));
    }

    MDB_txn* txn;
    rc = mdb_txn_begin(env_, nullptr, 0, &txn);
    if (rc != 0) {
        mdb_env_close(env_);
        env_ = nullptr;
        return std::unexpected(map_error(rc));
    }

    rc = mdb_dbi_open(txn, nullptr, 0, &dbi_);
    if (rc != 0) {
        mdb_txn_abort(txn);
        mdb_env_close(env_);
        env_ = nullptr;
        return std::unexpected(map_error(rc));
    }

    mdb_txn_commit(txn);
    dbi_opened_ = true;

    return {};
}

void KvStorage::close() {
    if (dbi_opened_ && env_) {
        mdb_dbi_close(env_, dbi_);
        dbi_opened_ = false;
    }
    if (env_) {
        mdb_env_close(env_);
        env_ = nullptr;
    }
}

bool KvStorage::is_open() const {
    return env_ != nullptr && dbi_opened_;
}

std::expected<void, KvError> KvStorage::put(const std::string& key, const std::string& value) {
    if (!is_open()) {
        return std::unexpected(KvError::OpenFailed);
    }

    MDB_txn* txn;
    int      rc = mdb_txn_begin(env_, nullptr, 0, &txn);
    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    MDB_val k = { key.size(), const_cast<char*>(key.data()) };
    MDB_val v = { value.size(), const_cast<char*>(value.data()) };

    rc = mdb_put(txn, dbi_, &k, &v, 0);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return std::unexpected(map_error(rc));
    }

    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    return {};
}

std::expected<std::string, KvError> KvStorage::get(const std::string& key) {
    if (!is_open()) {
        return std::unexpected(KvError::OpenFailed);
    }

    MDB_txn* txn;
    int      rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    MDB_val k = { key.size(), const_cast<char*>(key.data()) };
    MDB_val v;

    rc = mdb_get(txn, dbi_, &k, &v);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return std::unexpected(map_error(rc));
    }

    std::string result(static_cast<char*>(v.mv_data), v.mv_size);
    mdb_txn_abort(txn);

    return result;
}

std::expected<void, KvError> KvStorage::del(const std::string& key) {
    if (!is_open()) {
        return std::unexpected(KvError::OpenFailed);
    }

    MDB_txn* txn;
    int      rc = mdb_txn_begin(env_, nullptr, 0, &txn);
    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    MDB_val k = { key.size(), const_cast<char*>(key.data()) };

    rc = mdb_del(txn, dbi_, &k, nullptr);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return std::unexpected(map_error(rc));
    }

    mdb_txn_commit(txn);
    return {};
}

bool KvStorage::exists(const std::string& key) {
    return get(key).has_value();
}

std::expected<KvStorage::RangeResult, KvError>
KvStorage::range(const std::string& from, const std::string& to) {
    if (!is_open()) {
        return std::unexpected(KvError::OpenFailed);
    }

    MDB_txn* txn;
    int      rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    MDB_cursor* cursor;
    rc = mdb_cursor_open(txn, dbi_, &cursor);
    if (rc != 0) {
        mdb_txn_abort(txn);
        return std::unexpected(KvError::CursorFailed);
    }

    RangeResult result;
    MDB_val     k = { from.size(), const_cast<char*>(from.data()) };
    MDB_val     v;

    rc = mdb_cursor_get(cursor, &k, &v, MDB_SET_RANGE);
    while (rc == 0) {
        std::string key(static_cast<char*>(k.mv_data), k.mv_size);
        if (key > to) {
            break;
        }
        std::string value(static_cast<char*>(v.mv_data), v.mv_size);
        result.emplace_back(std::move(key), std::move(value));
        rc = mdb_cursor_get(cursor, &k, &v, MDB_NEXT);
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);

    return result;
}

std::expected<size_t, KvError> KvStorage::count() {
    if (!is_open()) {
        return std::unexpected(KvError::OpenFailed);
    }

    MDB_txn* txn;
    int      rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    MDB_stat stat;
    rc = mdb_stat(txn, dbi_, &stat);
    mdb_txn_abort(txn);

    if (rc != 0) {
        return std::unexpected(map_error(rc));
    }

    return stat.ms_entries;
}

KvError KvStorage::map_error(int rc) {
    switch (rc) {
    case MDB_NOTFOUND: return KvError::NotFound;
    case MDB_MAP_FULL: return KvError::MapFull;
    case MDB_TXN_FULL:
    case MDB_DBS_FULL: return KvError::TransactionFailed;
    case ENOENT:
    case EACCES: return KvError::OpenFailed;
    default: return KvError::Unknown;
    }
}

} // namespace Utils
