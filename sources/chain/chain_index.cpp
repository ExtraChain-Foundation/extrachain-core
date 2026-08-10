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

#include "chain/chain_index.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <unordered_map>

#include "chain/dag.h"
#include "chain/transaction.h"
#include "contracts/contract_transaction.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace {

    constexpr const char *DB_FILENAME = "ChainIndex.db";

    // actor/token are integer FKs into small lookup tables. The blob->id mapping
    // is cached in-process so steady-state writes cost one prepared INSERT per tx.
    constexpr const char *SCHEMA_SQL = R"(
    CREATE TABLE IF NOT EXISTS actors (
        id    INTEGER PRIMARY KEY,
        actor BLOB    UNIQUE NOT NULL
    );
    CREATE TABLE IF NOT EXISTS tokens (
        id    INTEGER PRIMARY KEY,
        actor BLOB    UNIQUE NOT NULL
    );
    CREATE TABLE IF NOT EXISTS tx_index (
        id        INTEGER PRIMARY KEY,
        section   INTEGER NOT NULL,
        sender    INTEGER NOT NULL,
        receiver  INTEGER NOT NULL,
        token     INTEGER NOT NULL,
        type      INTEGER NOT NULL,
        timestamp INTEGER NOT NULL,
        amount    TEXT    NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_sender   ON tx_index(sender, timestamp DESC);
    CREATE INDEX IF NOT EXISTS idx_receiver ON tx_index(receiver, timestamp DESC);
    CREATE INDEX IF NOT EXISTS idx_section  ON tx_index(section);
    CREATE TABLE IF NOT EXISTS contract_tx_index (
        section  INTEGER NOT NULL,
        hash     TEXT    NOT NULL,
        contract TEXT    NOT NULL,
        PRIMARY KEY (section, hash, contract)
    );
    CREATE INDEX IF NOT EXISTS idx_contract_section
        ON contract_tx_index(contract, section);
    CREATE TABLE IF NOT EXISTS index_meta (
        key   TEXT PRIMARY KEY,
        value TEXT NOT NULL
    );
)";

    constexpr const char *DERIVED_INDEX_VERSION = "2";
    constexpr std::size_t LIVE_WRITE_BATCH_SIZE = 128;
    constexpr auto        LIVE_WRITE_BATCH_AGE  = std::chrono::milliseconds(250);

    std::uint64_t section_to_u64(const SectionId &id) {
        auto i = id.to_int();
        if (i.has_value() && *i >= 0)
            return static_cast<std::uint64_t>(*i);
        try {
            return std::stoull(id.to_string());
        } catch (...) {
            return 0;
        }
    }

    // Hex string -> raw bytes. Returns empty on malformed input.
    std::vector<std::uint8_t> hex_to_blob(const std::string &hex) {
        std::vector<std::uint8_t> out;
        if (hex.size() % 2 != 0)
            return out;
        out.reserve(hex.size() / 2);
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            int hi = nibble(hex[i]);
            int lo = nibble(hex[i + 1]);
            if (hi < 0 || lo < 0)
                return {};
            out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        }
        return out;
    }

    std::string blob_to_hex(const void *data, int size) {
        static constexpr char digits[] = "0123456789abcdef";
        const auto           *p        = static_cast<const std::uint8_t *>(data);
        std::string           out;
        out.resize(size * 2);
        for (int i = 0; i < size; ++i) {
            out[i * 2]     = digits[p[i] >> 4];
            out[i * 2 + 1] = digits[p[i] & 0xF];
        }
        return out;
    }

    // Caches blob -> rowid for actors/tokens. Used on hot write path so the typical
    // case (actor seen recently) is one map lookup, not a SELECT.
    struct BlobIdCache {
        struct ByteVecHash {
            std::size_t operator()(const std::vector<std::uint8_t> &v) const noexcept {
                std::size_t h = 1469598103934665603ull; // FNV-1a 64
                for (auto b : v) {
                    h ^= b;
                    h *= 1099511628211ull;
                }
                return h;
            }
        };
        std::unordered_map<std::vector<std::uint8_t>, sqlite3_int64, ByteVecHash> map;
    };

} // namespace

struct ChainIndex::Impl {
    ExtraChainNode *node = nullptr;
    sqlite3        *db   = nullptr;

    sqlite3_stmt *stmt_insert_tx               = nullptr;
    sqlite3_stmt *stmt_delete_section          = nullptr;
    sqlite3_stmt *stmt_find_sent               = nullptr;
    sqlite3_stmt *stmt_find_sent_token         = nullptr;
    sqlite3_stmt *stmt_find_recv               = nullptr;
    sqlite3_stmt *stmt_find_recv_token         = nullptr;
    sqlite3_stmt *stmt_row_count               = nullptr;
    sqlite3_stmt *stmt_last_section            = nullptr;
    sqlite3_stmt *stmt_delete_contract_section = nullptr;
    sqlite3_stmt *stmt_insert_contract_tx      = nullptr;
    sqlite3_stmt *stmt_find_contract_sections  = nullptr;
    sqlite3_stmt *stmt_actor_select            = nullptr;
    sqlite3_stmt *stmt_actor_insert            = nullptr;
    sqlite3_stmt *stmt_token_select            = nullptr;
    sqlite3_stmt *stmt_token_insert            = nullptr;
    sqlite3_stmt *stmt_actor_blob_by_id        = nullptr;
    sqlite3_stmt *stmt_token_blob_by_id        = nullptr;
    sqlite3_stmt *stmt_count_actor_type_since  = nullptr;

    mutable std::mutex                    write_mutex;
    bool                                  derived_index_ready  = false;
    bool                                  write_batch_open     = false;
    std::size_t                           write_batch_sections = 0;
    std::chrono::steady_clock::time_point write_batch_started;

    BlobIdCache actor_cache;
    BlobIdCache token_cache;

    ~Impl() {
        if (write_batch_open && !exec("COMMIT")) {
            exec("ROLLBACK");
        }
        write_batch_open     = false;
        write_batch_sections = 0;

        auto finalize = [](sqlite3_stmt *&s) {
            if (s)
                sqlite3_finalize(s);
            s = nullptr;
        };
        finalize(stmt_insert_tx);
        finalize(stmt_delete_section);
        finalize(stmt_find_sent);
        finalize(stmt_find_sent_token);
        finalize(stmt_find_recv);
        finalize(stmt_find_recv_token);
        finalize(stmt_row_count);
        finalize(stmt_last_section);
        finalize(stmt_delete_contract_section);
        finalize(stmt_insert_contract_tx);
        finalize(stmt_find_contract_sections);
        finalize(stmt_actor_select);
        finalize(stmt_actor_insert);
        finalize(stmt_token_select);
        finalize(stmt_token_insert);
        finalize(stmt_actor_blob_by_id);
        finalize(stmt_token_blob_by_id);
        finalize(stmt_count_actor_type_since);
        if (db) {
            sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
            sqlite3_close(db);
        }
    }

    bool exec(const char *sql) {
        char *err = nullptr;
        int   rc  = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "?";
            sqlite3_free(err);
            eWarning("[ChainIndex] SQL error: {} | sql: {}", msg, sql);
            return false;
        }
        return true;
    }

    bool begin_write_batch() {
        if (write_batch_open)
            return true;
        if (!exec("BEGIN IMMEDIATE"))
            return false;
        write_batch_open     = true;
        write_batch_sections = 0;
        write_batch_started  = std::chrono::steady_clock::now();
        return true;
    }

    void rollback_write_batch() {
        if (write_batch_open)
            exec("ROLLBACK");
        write_batch_open     = false;
        write_batch_sections = 0;
        // IDs inserted by the rolled-back transaction no longer exist. Clear
        // both derived caches so the next write resolves committed IDs again.
        actor_cache.map.clear();
        token_cache.map.clear();
    }

    bool flush_write_batch() {
        if (!write_batch_open)
            return true;
        if (!exec("COMMIT")) {
            rollback_write_batch();
            return false;
        }
        write_batch_open     = false;
        write_batch_sections = 0;
        return true;
    }

    bool finish_section_write() {
        ++write_batch_sections;
        const auto age = std::chrono::steady_clock::now() - write_batch_started;
        if (write_batch_sections < LIVE_WRITE_BATCH_SIZE && age < LIVE_WRITE_BATCH_AGE)
            return true;
        return flush_write_batch();
    }

    sqlite3_stmt *prepare(const char *sql) {
        sqlite3_stmt *s = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
            eWarning("[ChainIndex] prepare failed: {} | sql: {}", sqlite3_errmsg(db), sql);
            return nullptr;
        }
        return s;
    }

    bool open() {
        std::filesystem::create_directories(ChainConst::DAG_CACHE_FOLDER);
        auto path = ChainConst::DAG_CACHE_FOLDER + "/" + DB_FILENAME;

        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            eWarning("[ChainIndex] Failed to open {}: {}", path, sqlite3_errmsg(db));
            sqlite3_close(db);
            db = nullptr;
            return false;
        }

        exec("PRAGMA journal_mode = WAL");
        exec("PRAGMA synchronous = NORMAL");
        exec("PRAGMA temp_store = MEMORY");
        exec("PRAGMA cache_size = -32000");
        const auto full_node = node == nullptr || node->runtime_profile() == RuntimeProfile::FullNode;
        sqlite3_wal_autocheckpoint(db, full_node ? 4096 : 1024);
        exec(full_node ? "PRAGMA journal_size_limit = 67108864" : "PRAGMA journal_size_limit = 16777216");

        if (!exec(SCHEMA_SQL)) {
            return false;
        }

        bool          has_hash = false;
        sqlite3_stmt *columns  = nullptr;
        if (sqlite3_prepare_v2(db, "PRAGMA table_info(tx_index)", -1, &columns, nullptr) == SQLITE_OK) {
            while (sqlite3_step(columns) == SQLITE_ROW) {
                const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(columns, 1));
                if (name != nullptr && std::string_view(name) == "hash") {
                    has_hash = true;
                    break;
                }
            }
        }
        if (columns != nullptr)
            sqlite3_finalize(columns);
        if (has_hash) {
            if (!exec("BEGIN IMMEDIATE") || !exec("ALTER TABLE tx_index RENAME TO tx_index_with_hash")
                || !exec("CREATE TABLE tx_index ("
                         "id INTEGER PRIMARY KEY, section INTEGER NOT NULL, sender INTEGER NOT NULL,"
                         "receiver INTEGER NOT NULL, token INTEGER NOT NULL, type INTEGER NOT NULL,"
                         "timestamp INTEGER NOT NULL, amount TEXT NOT NULL)")
                || !exec("INSERT INTO tx_index(id,section,sender,receiver,token,type,timestamp,amount)"
                         " SELECT id,section,sender,receiver,token,type,timestamp,amount FROM tx_index_with_hash")
                || !exec("DROP TABLE tx_index_with_hash")
                || !exec("CREATE INDEX idx_sender ON tx_index(sender, timestamp DESC)")
                || !exec("CREATE INDEX idx_receiver ON tx_index(receiver, timestamp DESC)")
                || !exec("CREATE INDEX idx_section ON tx_index(section)")
                || !exec("UPDATE index_meta SET value='2'"
                         " WHERE key='derived_index_version' AND value='1'")
                || !exec("COMMIT")) {
                exec("ROLLBACK");
                return false;
            }
        }

        sqlite3_stmt *row_count = nullptr;
        sqlite3_stmt *version   = nullptr;
        sqlite3_int64 rows      = 0;
        std::string   stored_version;
        if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM tx_index", -1, &row_count, nullptr) == SQLITE_OK
            && sqlite3_step(row_count) == SQLITE_ROW) {
            rows = sqlite3_column_int64(row_count, 0);
        }
        if (row_count != nullptr)
            sqlite3_finalize(row_count);
        if (sqlite3_prepare_v2(db,
                               "SELECT value FROM index_meta WHERE key = 'derived_index_version'",
                               -1,
                               &version,
                               nullptr)
                == SQLITE_OK
            && sqlite3_step(version) == SQLITE_ROW) {
            const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(version, 0));
            if (value != nullptr)
                stored_version = value;
        }
        if (version != nullptr)
            sqlite3_finalize(version);
        derived_index_ready = stored_version == DERIVED_INDEX_VERSION;
        if (rows == 0 && !derived_index_ready) {
            derived_index_ready = exec(
                "INSERT OR REPLACE INTO index_meta(key, value)"
                " VALUES ('derived_index_version', '2')");
        }

        // Prepared statements
        stmt_insert_tx = prepare(
            "INSERT INTO tx_index"
            " (section, sender, receiver, token, type, timestamp, amount)"
            " VALUES (?, ?, ?, ?, ?, ?, ?)");
        stmt_delete_section = prepare("DELETE FROM tx_index WHERE section = ?");

        // Column order in every SELECT below must match row_to_entry().
        const char *select_cols =
            "SELECT t.section, sa.actor, ra.actor, tk.actor, t.type, t.timestamp, t.amount"
            " FROM tx_index t"
            " JOIN actors sa ON sa.id = t.sender"
            " JOIN actors ra ON ra.id = t.receiver"
            " JOIN tokens tk ON tk.id = t.token";

        auto build = [&](const char *where) {
            std::string sql = select_cols;
            sql += " ";
            sql += where;
            return prepare(sql.c_str());
        };

        stmt_find_sent = build(
            "WHERE t.sender = ? AND (? = 0 OR t.timestamp < ?)"
            " ORDER BY t.timestamp DESC LIMIT ?");
        stmt_find_sent_token = build(
            "WHERE t.sender = ? AND t.token = ? AND (? = 0 OR t.timestamp < ?)"
            " ORDER BY t.timestamp DESC LIMIT ?");
        stmt_find_recv = build(
            "WHERE t.receiver = ? AND (? = 0 OR t.timestamp < ?)"
            " ORDER BY t.timestamp DESC LIMIT ?");
        stmt_find_recv_token = build(
            "WHERE t.receiver = ? AND t.token = ? AND (? = 0 OR t.timestamp < ?)"
            " ORDER BY t.timestamp DESC LIMIT ?");

        stmt_row_count               = prepare("SELECT COUNT(*) FROM tx_index");
        stmt_last_section            = prepare("SELECT COALESCE(MAX(section), -1) FROM tx_index");
        stmt_delete_contract_section = prepare("DELETE FROM contract_tx_index WHERE section = ?");
        stmt_insert_contract_tx =
            prepare("INSERT OR IGNORE INTO contract_tx_index(section, hash, contract) VALUES (?, ?, ?)");
        stmt_find_contract_sections = prepare(
            "SELECT DISTINCT section FROM contract_tx_index"
            " WHERE contract = ? AND section >= ? ORDER BY section");

        stmt_actor_select           = prepare("SELECT id FROM actors WHERE actor = ?");
        stmt_actor_insert           = prepare("INSERT INTO actors(actor) VALUES (?)");
        stmt_actor_blob_by_id       = prepare("SELECT actor FROM actors WHERE id = ?");
        stmt_token_select           = prepare("SELECT id FROM tokens WHERE actor = ?");
        stmt_token_insert           = prepare("INSERT INTO tokens(actor) VALUES (?)");
        stmt_token_blob_by_id       = prepare("SELECT actor FROM tokens WHERE id = ?");
        stmt_count_actor_type_since = prepare(
            "SELECT COUNT(*) FROM tx_index"
            " WHERE type = ? AND timestamp >= ? AND (sender = ? OR receiver = ?)");

        return stmt_insert_tx && stmt_delete_section && stmt_find_sent && stmt_find_sent_token && stmt_find_recv
               && stmt_find_recv_token && stmt_row_count && stmt_last_section && stmt_delete_contract_section
               && stmt_insert_contract_tx && stmt_find_contract_sections && stmt_actor_select && stmt_actor_insert
               && stmt_token_select && stmt_token_insert && stmt_actor_blob_by_id && stmt_token_blob_by_id
               && stmt_count_actor_type_since;
    }

    bool should_index(const Transaction &tx) const {
        if (!node)
            return true;
        auto *dag = node->dag();
        if (!dag || dag->mode() == DagMode::Full)
            return true;
        auto *ac = node->account_controller();
        if (!ac || ac->empty())
            return false;
        auto actors = ac->accounts_ids();
        for (const auto &a : actors) {
            if (tx.sender() == a || tx.receiver() == a)
                return true;
        }
        return false;
    }

    sqlite3_int64 get_or_create_id(BlobIdCache                     &cache,
                                   sqlite3_stmt                    *select,
                                   sqlite3_stmt                    *insert,
                                   const std::vector<std::uint8_t> &blob) {
        auto it = cache.map.find(blob);
        if (it != cache.map.end())
            return it->second;

        sqlite3_reset(select);
        sqlite3_bind_blob(select, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(select) == SQLITE_ROW) {
            sqlite3_int64 id = sqlite3_column_int64(select, 0);
            sqlite3_reset(select);
            cache.map.emplace(blob, id);
            return id;
        }
        sqlite3_reset(select);

        sqlite3_reset(insert);
        sqlite3_bind_blob(insert, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
        if (sqlite3_step(insert) != SQLITE_DONE) {
            eWarning("[ChainIndex] actor/token insert failed: {}", sqlite3_errmsg(db));
            return -1;
        }
        sqlite3_int64 id = sqlite3_last_insert_rowid(db);
        cache.map.emplace(blob, id);
        return id;
    }

    bool insert_tx(const Transaction &tx, const SectionId &section_id) {
        if (!should_index(tx))
            return true;

        auto sender_blob   = hex_to_blob(tx.sender().to_string());
        auto receiver_blob = hex_to_blob(tx.receiver().to_string());
        auto token_blob    = hex_to_blob(tx.token().to_string());
        if (sender_blob.empty() || receiver_blob.empty() || token_blob.empty()) {
            return false;
        }

        auto sender_id   = get_or_create_id(actor_cache, stmt_actor_select, stmt_actor_insert, sender_blob);
        auto receiver_id = get_or_create_id(actor_cache, stmt_actor_select, stmt_actor_insert, receiver_blob);
        auto token_id    = get_or_create_id(token_cache, stmt_token_select, stmt_token_insert, token_blob);
        if (sender_id < 0 || receiver_id < 0 || token_id < 0)
            return false;

        std::string amount = tx.amount().to_string();

        auto *s = stmt_insert_tx;
        sqlite3_reset(s);
        sqlite3_bind_int64(s, 1, static_cast<sqlite3_int64>(section_to_u64(section_id)));
        sqlite3_bind_int64(s, 2, sender_id);
        sqlite3_bind_int64(s, 3, receiver_id);
        sqlite3_bind_int64(s, 4, token_id);
        sqlite3_bind_int(s, 5, static_cast<int>(tx.type()));
        sqlite3_bind_int64(s, 6, static_cast<sqlite3_int64>(tx.timestamp()));
        sqlite3_bind_text(s, 7, amount.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(s) != SQLITE_DONE) {
            eWarning("[ChainIndex] insert failed: {}", sqlite3_errmsg(db));
            return false;
        }

        if (!is_contract_transaction(tx.type()))
            return true;
        if (!tx.meta().has_value())
            return false;
        const auto metadata = Json::deserialize<ContractTransactionData>(*tx.meta());
        if (!metadata.has_value() || metadata->schema != 4)
            return false;

        std::vector<std::string> contracts;
        contracts.reserve(metadata->transitions.size() + 1);
        contracts.push_back(tx.receiver().to_string());
        for (const auto &transition : metadata->transitions) {
            if (!transition.contract_id.empty())
                contracts.push_back(transition.contract_id);
        }
        std::ranges::sort(contracts);
        contracts.erase(std::ranges::unique(contracts).begin(), contracts.end());
        for (const auto &contract : contracts) {
            sqlite3_reset(stmt_insert_contract_tx);
            sqlite3_clear_bindings(stmt_insert_contract_tx);
            sqlite3_bind_int64(stmt_insert_contract_tx, 1, static_cast<sqlite3_int64>(section_to_u64(section_id)));
            sqlite3_bind_text(stmt_insert_contract_tx, 2, tx.hash().c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt_insert_contract_tx, 3, contract.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt_insert_contract_tx) != SQLITE_DONE) {
                eWarning("[ChainIndex] contract insert failed: {}", sqlite3_errmsg(db));
                return false;
            }
        }
        return true;
    }

    static ChainIndexEntry row_to_entry(sqlite3_stmt *s) {
        // Column order: section, sender_blob, receiver_blob, token_blob, type,
        // timestamp, amount. Hash isn't stored — callers fetch it via Dag if needed.
        ChainIndexEntry e;
        e.section_id             = SectionId(static_cast<long long>(sqlite3_column_int64(s, 0)));
        e.sender                 = blob_to_hex(sqlite3_column_blob(s, 1), sqlite3_column_bytes(s, 1));
        e.receiver               = blob_to_hex(sqlite3_column_blob(s, 2), sqlite3_column_bytes(s, 2));
        e.token                  = blob_to_hex(sqlite3_column_blob(s, 3), sqlite3_column_bytes(s, 3));
        e.type                   = sqlite3_column_int(s, 4);
        e.timestamp              = static_cast<std::uint64_t>(sqlite3_column_int64(s, 5));
        const unsigned char *amt = sqlite3_column_text(s, 6);
        e.amount                 = amt ? reinterpret_cast<const char *>(amt) : "";
        return e;
    }

    std::vector<ChainIndexEntry> run_select_one_side(sqlite3_stmt      *no_token,
                                                     sqlite3_stmt      *with_token,
                                                     const std::string &actor_hex,
                                                     const std::string &token_hex,
                                                     std::uint64_t      before_ts,
                                                     int                limit) const {
        std::vector<ChainIndexEntry> out;
        out.reserve(limit);

        auto actor_blob = hex_to_blob(actor_hex);
        if (actor_blob.empty())
            return out;

        // Resolve actor blob -> id once. If the actor has never been seen,
        // there's nothing to return.
        auto find_id = [this](sqlite3_stmt *select, const std::vector<std::uint8_t> &blob) -> sqlite3_int64 {
            sqlite3_reset(select);
            sqlite3_bind_blob(select, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
            if (sqlite3_step(select) == SQLITE_ROW) {
                const auto id = sqlite3_column_int64(select, 0);
                sqlite3_reset(select);
                return id;
            }
            sqlite3_reset(select);
            return -1;
        };

        auto actor_id = find_id(stmt_actor_select, actor_blob);
        if (actor_id < 0)
            return out;

        sqlite3_int64 token_id = -1;
        if (!token_hex.empty()) {
            auto token_blob = hex_to_blob(token_hex);
            if (token_blob.empty())
                return out;
            token_id = find_id(stmt_token_select, token_blob);
            if (token_id < 0)
                return out;
        }

        sqlite3_stmt *s = token_hex.empty() ? no_token : with_token;
        sqlite3_reset(s);

        int idx = 1;
        sqlite3_bind_int64(s, idx++, actor_id);
        if (!token_hex.empty()) {
            sqlite3_bind_int64(s, idx++, token_id);
        }
        // Pattern: (? = 0 OR timestamp < ?) — two binds for the same value,
        // 0 means "no upper bound".
        sqlite3_bind_int64(s, idx++, static_cast<sqlite3_int64>(before_ts));
        sqlite3_bind_int64(s, idx++, static_cast<sqlite3_int64>(before_ts));
        sqlite3_bind_int(s, idx++, limit);

        while (sqlite3_step(s) == SQLITE_ROW) {
            out.push_back(row_to_entry(s));
        }
        sqlite3_reset(s);
        return out;
    }
};

ChainIndex::ChainIndex(ExtraChainNode *node)
    : impl_(std::make_unique<Impl>()) {
    impl_->node = node;
    if (!impl_->open()) {
        eCritical("[ChainIndex] Failed to initialize index database");
    }
}

ChainIndex::~ChainIndex() = default;

void ChainIndex::on_section_written(const Section &s) {
    if (!impl_->db)
        return;

    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    if (!impl_->begin_write_batch())
        return;
    auto section = static_cast<sqlite3_int64>(section_to_u64(s.id));
    sqlite3_reset(impl_->stmt_delete_section);
    sqlite3_bind_int64(impl_->stmt_delete_section, 1, section);
    if (sqlite3_step(impl_->stmt_delete_section) != SQLITE_DONE) {
        eWarning("[ChainIndex] section cleanup failed: {}", sqlite3_errmsg(impl_->db));
        impl_->rollback_write_batch();
        return;
    }
    sqlite3_reset(impl_->stmt_delete_contract_section);
    sqlite3_bind_int64(impl_->stmt_delete_contract_section, 1, section);
    if (sqlite3_step(impl_->stmt_delete_contract_section) != SQLITE_DONE) {
        eWarning("[ChainIndex] contract section cleanup failed: {}", sqlite3_errmsg(impl_->db));
        impl_->rollback_write_batch();
        return;
    }
    for (const auto &tx : s.transactions) {
        if (!impl_->insert_tx(tx, s.id)) {
            impl_->rollback_write_batch();
            return;
        }
    }
    impl_->finish_section_write();
}

std::vector<ChainIndexEntry> ChainIndex::find_sent_by(const std::string &actor,
                                                      const std::string &token,
                                                      std::uint64_t      before_timestamp,
                                                      int                limit) const {
    if (!impl_->db)
        return {};
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    return impl_->run_select_one_side(impl_->stmt_find_sent,
                                      impl_->stmt_find_sent_token,
                                      actor,
                                      token,
                                      before_timestamp,
                                      limit);
}

std::vector<ChainIndexEntry> ChainIndex::find_received_by(const std::string &actor,
                                                          const std::string &token,
                                                          std::uint64_t      before_timestamp,
                                                          int                limit) const {
    if (!impl_->db)
        return {};
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    return impl_->run_select_one_side(impl_->stmt_find_recv,
                                      impl_->stmt_find_recv_token,
                                      actor,
                                      token,
                                      before_timestamp,
                                      limit);
}

std::vector<SectionId> ChainIndex::find_contract_sections(const std::string &contract,
                                                          const SectionId   &from_section) const {
    std::vector<SectionId> sections;
    if (!impl_->db || !impl_->stmt_find_contract_sections || contract.empty())
        return sections;
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    auto                       *statement = impl_->stmt_find_contract_sections;
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    sqlite3_bind_text(statement, 1, contract.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(section_to_u64(from_section)));
    while (sqlite3_step(statement) == SQLITE_ROW) {
        sections.emplace_back(static_cast<long long>(sqlite3_column_int64(statement, 0)));
    }
    sqlite3_reset(statement);
    return sections;
}

bool ChainIndex::derived_index_ready() const {
    if (!impl_->db)
        return false;
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    return impl_->derived_index_ready;
}

std::vector<ChainIndexEntry> ChainIndex::find_for_actor(const std::string &actor,
                                                        const std::string &token,
                                                        std::uint64_t      before_timestamp,
                                                        int                limit) const {
    if (!impl_->db)
        return {};

    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    auto                        sent = impl_->run_select_one_side(impl_->stmt_find_sent,
                                           impl_->stmt_find_sent_token,
                                           actor,
                                           token,
                                           before_timestamp,
                                           limit);
    auto                        recv = impl_->run_select_one_side(impl_->stmt_find_recv,
                                           impl_->stmt_find_recv_token,
                                           actor,
                                           token,
                                           before_timestamp,
                                           limit);

    std::vector<ChainIndexEntry> merged;
    merged.reserve(sent.size() + recv.size());
    merged.insert(merged.end(), sent.begin(), sent.end());
    merged.insert(merged.end(), recv.begin(), recv.end());

    std::sort(merged.begin(), merged.end(), [](const ChainIndexEntry &a, const ChainIndexEntry &b) {
        return a.timestamp > b.timestamp;
    });
    // Deduplicate self-transfers (sender == receiver of the same tx appears
    // in both sent/recv result sets). Match on the natural identity columns.
    auto same_tx = [](const ChainIndexEntry &a, const ChainIndexEntry &b) {
        return a.section_id == b.section_id && a.timestamp == b.timestamp && a.sender == b.sender
               && a.receiver == b.receiver && a.token == b.token && a.type == b.type && a.amount == b.amount;
    };
    std::vector<ChainIndexEntry> out;
    out.reserve(merged.size());
    for (auto &e : merged) {
        if (!out.empty() && same_tx(out.back(), e))
            continue;
        out.push_back(std::move(e));
        if (static_cast<int>(out.size()) >= limit)
            break;
    }
    return out;
}

std::uint64_t ChainIndex::count_for_actor_by_type_since(const std::string &actor,
                                                        int                type,
                                                        std::uint64_t      since_timestamp) const {
    if (!impl_->db || !impl_->stmt_actor_select || !impl_->stmt_count_actor_type_since)
        return 0;

    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    const auto                  actor_blob = hex_to_blob(actor);
    if (actor_blob.empty())
        return 0;

    sqlite3_reset(impl_->stmt_actor_select);
    sqlite3_clear_bindings(impl_->stmt_actor_select);
    sqlite3_bind_blob(impl_->stmt_actor_select,
                      1,
                      actor_blob.data(),
                      static_cast<int>(actor_blob.size()),
                      SQLITE_TRANSIENT);
    if (sqlite3_step(impl_->stmt_actor_select) != SQLITE_ROW) {
        sqlite3_reset(impl_->stmt_actor_select);
        return 0;
    }
    const auto actor_id = sqlite3_column_int64(impl_->stmt_actor_select, 0);
    sqlite3_reset(impl_->stmt_actor_select);

    auto *statement = impl_->stmt_count_actor_type_since;
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    sqlite3_bind_int(statement, 1, type);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(since_timestamp));
    sqlite3_bind_int64(statement, 3, actor_id);
    sqlite3_bind_int64(statement, 4, actor_id);
    const auto result =
        sqlite3_step(statement) == SQLITE_ROW ? static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0)) : 0;
    sqlite3_reset(statement);
    return result;
}

void ChainIndex::rebuild_from_disk() {
    if (!impl_->db || !impl_->node)
        return;

    auto *dag = impl_->node->dag();
    if (!dag)
        return;

    eLog("[ChainIndex] Rebuild started");

    std::lock_guard<std::mutex> lock(impl_->write_mutex);

    impl_->flush_write_batch();

    impl_->exec("PRAGMA journal_mode = MEMORY");
    impl_->exec("PRAGMA synchronous = OFF");
    struct Restore {
        Impl *i;
        ~Restore() {
            i->exec("PRAGMA synchronous = NORMAL");
            i->exec("PRAGMA journal_mode = WAL");
        }
    } restore_on_exit { impl_.get() };

    impl_->derived_index_ready = false;
    impl_->exec("INSERT OR REPLACE INTO index_meta(key, value) VALUES ('derived_index_version', 'rebuilding')");
    impl_->exec("DELETE FROM tx_index");
    impl_->exec("DELETE FROM contract_tx_index");
    impl_->exec("DELETE FROM actors");
    impl_->exec("DELETE FROM tokens");
    impl_->actor_cache.map.clear();
    impl_->token_cache.map.clear();

    if (!impl_->exec("BEGIN IMMEDIATE"))
        return;

    const SectionId first = dag->first_saved_section();
    const SectionId last  = dag->current_section();
    std::uint64_t   count = 0;

    for (SectionId i = first; i <= last; i = i + 1) {
        auto section = dag->read_section(i);
        if (!section.has_value())
            continue;
        for (const auto &tx : section->transactions) {
            if (!impl_->insert_tx(tx, section->id)) {
                impl_->exec("ROLLBACK");
                return;
            }
            count++;
            if (count % 10000 == 0) {
                if (!impl_->exec("COMMIT") || !impl_->exec("BEGIN IMMEDIATE")) {
                    impl_->exec("ROLLBACK");
                    return;
                }
                eLog("[ChainIndex] Rebuild progress: {} tx", count);
            }
        }
    }
    if (!impl_->exec("COMMIT")) {
        impl_->exec("ROLLBACK");
        return;
    }
    impl_->exec("ANALYZE");
    impl_->derived_index_ready =
        impl_->exec("INSERT OR REPLACE INTO index_meta(key, value) VALUES ('derived_index_version', '2')");

    eLog("[ChainIndex] Rebuild done, {} tx, {} actors, {} tokens",
         count,
         impl_->actor_cache.map.size(),
         impl_->token_cache.map.size());
}

void ChainIndex::clear() {
    if (!impl_->db)
        return;
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    impl_->flush_write_batch();
    impl_->exec("DELETE FROM tx_index");
    impl_->exec("DELETE FROM contract_tx_index");
    impl_->exec("DELETE FROM actors");
    impl_->exec("DELETE FROM tokens");
    impl_->derived_index_ready =
        impl_->exec("INSERT OR REPLACE INTO index_meta(key, value) VALUES ('derived_index_version', '2')");
    impl_->actor_cache.map.clear();
    impl_->token_cache.map.clear();
}

void ChainIndex::flush() {
    if (!impl_->db)
        return;
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    impl_->flush_write_batch();
}

std::uint64_t ChainIndex::row_count() const {
    if (!impl_->db || !impl_->stmt_row_count)
        return 0;
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    auto                       *s = impl_->stmt_row_count;
    sqlite3_reset(s);
    if (sqlite3_step(s) == SQLITE_ROW) {
        const auto rows = static_cast<std::uint64_t>(sqlite3_column_int64(s, 0));
        sqlite3_reset(s);
        return rows;
    }
    sqlite3_reset(s);
    return 0;
}

SectionId ChainIndex::last_indexed_section() const {
    if (!impl_->db || !impl_->stmt_last_section)
        return SectionId(-1);
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    auto                       *s = impl_->stmt_last_section;
    sqlite3_reset(s);
    if (sqlite3_step(s) == SQLITE_ROW) {
        const auto section = SectionId(static_cast<long long>(sqlite3_column_int64(s, 0)));
        sqlite3_reset(s);
        return section;
    }
    sqlite3_reset(s);
    return SectionId(-1);
}
