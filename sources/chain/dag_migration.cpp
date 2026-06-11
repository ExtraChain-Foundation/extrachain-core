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

#include "chain/dag_migration.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <vector>

#include "chain/dag.h" // for SectionRange
#include "chain/pack.h"
#include "network/wire_format.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/db_connector.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace DagMigration {

namespace {

namespace fs = std::filesystem;

// On-disk layout of a legacy dag/ folder looks like:
//   dag/<hex_shard_id>/<hex_section_id>    one JSON per section, hex-named
//   dag/range                              {first,last,last_cached} in hex
//   dag/cache/BalanceCache.db              balance amounts stored in hex
//
// We want it to look like:
//   dag/hot/<decimal_section_id>           same JSON, decimal-named, still loose
//   dag/packs/<pack_id>.pack               immutable packed groups of SECTION_SIZE
//   dag/range                              decimal
//   dag/cache/BalanceCache.db              amounts in decimal

struct Checkpoint {
    std::uint64_t last_section_done; // inclusive; next to process = this + 1
    std::string   stage;             // "sections" | "packing" | "balance_cache"
};
BOOST_DESCRIBE_STRUCT(Checkpoint, (), (last_section_done, stage))

fs::path checkpoint_path() {
    return fs::path(ChainConst::DAG_FOLDER) / "migration.progress";
}

std::optional<Checkpoint> read_checkpoint() {
    std::ifstream f(checkpoint_path());
    if (!f) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(f)), {});
    auto parsed = Json::deserialize<Checkpoint>(content);
    if (!parsed.has_value()) return std::nullopt;
    return *parsed;
}

bool write_checkpoint(const Checkpoint &c) {
    std::ofstream f(checkpoint_path());
    if (!f) return false;
    f << Json::serialize(c);
    return static_cast<bool>(f);
}

void remove_checkpoint() {
    std::error_code ec;
    fs::remove(checkpoint_path(), ec);
}

// Detect whether a directory name is a hex (shard) rather than decimal.
// Old layout used hex-named shard folders; new uses "hot"/"packs"/"cache".
bool looks_like_hex_shard_dir(const fs::path &p) {
    if (!fs::is_directory(p)) return false;
    auto name = p.filename().string();
    if (name == "hot" || name == "packs" || name == "cache") return false;
    if (name.empty()) return false;
    // decimal-only: not interesting either way, but treat as maybe-legacy
    // We accept names that are either pure hex digits or decimal.
    for (char c : name) {
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

// A legacy section filename is the section id encoded in hex (no ".json" suffix).
std::optional<SectionId> parse_legacy_section_filename(const std::string &name) {
    if (name.empty()) return std::nullopt;
    for (char c : name) {
        bool hex = (c >= '0' && c <= '9')
            || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
        if (!hex) return std::nullopt;
    }
    return BigNumber::from_hex(name);
}

// Convert a legacy (hex-encoded) section JSON payload into the canonical
// (decimal) form: deserialize the numeric fields as hex, then re-serialize them
// as decimal. Hash/signature/control are plain strings and survive untouched, so
// signatures still verify via Transaction::calculate_hash_hex() (which recomputes
// the legacy hex hash from the now-decimal in-memory values).
//
// If the payload can't be parsed (unexpected legacy format), the original bytes
// are returned unchanged and the caller is warned — never silently dropped.
std::string convert_section_payload(const std::string &payload, const SectionId &sid, bool &ok) {
    std::optional<Section> deser;
    {
        WireFormat::Scope legacy(WireFormat::Mode::Legacy);
        auto              parsed = Json::deserialize<Section>(payload);
        if (parsed.has_value()) deser = std::move(parsed.value());
    }
    if (!deser.has_value()) {
        ok = false;
        return payload;
    }
    deser->id = sid;
    ok        = true;
    WireFormat::Scope canonical(WireFormat::Mode::Canonical);
    return Json::serialize(*deser);
}

std::expected<void, Error>
collect_legacy_sections(std::vector<std::pair<SectionId, fs::path>> &out) {
    fs::path dag_root = ChainConst::DAG_FOLDER;
    std::error_code ec;
    if (!fs::exists(dag_root, ec)) return std::unexpected(Error::DagNotFound);

    for (const auto &shard_entry : fs::directory_iterator(dag_root, ec)) {
        if (ec) break;
        if (!looks_like_hex_shard_dir(shard_entry.path())) continue;

        for (const auto &file_entry : fs::directory_iterator(shard_entry.path(), ec)) {
            if (ec) break;
            if (!file_entry.is_regular_file()) continue;
            auto name = file_entry.path().filename().string();
            if (name.starts_with(".")) continue;

            auto sid = parse_legacy_section_filename(name);
            if (!sid.has_value()) continue;
            out.emplace_back(*sid, file_entry.path());
        }
    }

    return {};
}

std::expected<void, Error>
migrate_sections(ProgressCallback on_progress) {
    std::vector<std::pair<SectionId, fs::path>> sections;
    auto collected = collect_legacy_sections(sections);
    if (!collected.has_value()) return std::unexpected(collected.error());

    std::sort(sections.begin(), sections.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    if (sections.empty()) return {}; // no legacy data

    fs::create_directories(ChainConst::DAG_HOT_FOLDER);
    fs::create_directories(ChainConst::DAG_PACKS_FOLDER);

    // Resume support
    auto cp           = read_checkpoint();
    SectionId resume  = (cp.has_value() && cp->stage == "sections")
                            ? SectionId(static_cast<long long>(cp->last_section_done)) + 1
                            : SectionId(-1);

    std::uint64_t total = sections.size();
    std::uint64_t done  = 0;

    for (const auto &[sid, src_path] : sections) {
        if (resume > SectionId(0) && sid < resume) {
            done++;
            continue;
        }

        std::ifstream in(src_path, std::ios::binary);
        if (!in) return std::unexpected(Error::ReadFailed);
        std::string content((std::istreambuf_iterator<char>(in)), {});
        in.close();

        std::string decimal_name = sid.to_string();
        fs::path    dest = fs::path(ChainConst::DAG_HOT_FOLDER) / decimal_name;

        bool        converted = false;
        std::string out_payload = convert_section_payload(content, sid, converted);
        if (!converted) {
            eWarning("[Migration] Section {} could not be parsed; kept verbatim", decimal_name);
        }

        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (!out) return std::unexpected(Error::WriteFailed);
        out << out_payload;
        out.close();
        if (!out) return std::unexpected(Error::WriteFailed);

        std::error_code ec;
        fs::remove(src_path, ec);

        done++;
        if (on_progress && (done % 500 == 0 || done == total)) {
            on_progress({ .processed = done, .total = total, .stage = "sections" });
        }

        // Checkpoint every 1000 sections
        if (done % 1000 == 0) {
            Checkpoint ncp {
                .last_section_done = static_cast<std::uint64_t>(
                    sid.to_int().value_or(static_cast<int>(std::stoull(sid.to_string())))),
                .stage = "sections",
            };
            if (!write_checkpoint(ncp)) {
                return std::unexpected(Error::CheckpointFailed);
            }
        }
    }

    // Clean up now-empty legacy shard dirs
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(ChainConst::DAG_FOLDER, ec)) {
        if (ec) break;
        if (!looks_like_hex_shard_dir(entry.path())) continue;
        std::error_code rmec;
        fs::remove_all(entry.path(), rmec);
    }

    return {};
}

// Pack consecutive ranges of SECTION_SIZE from hot/ into packs/.
std::expected<void, Error> pack_cold_sections(ProgressCallback on_progress) {
    fs::path hot_dir = ChainConst::DAG_HOT_FOLDER;
    std::error_code ec;
    if (!fs::exists(hot_dir, ec)) return {};

    std::vector<SectionId> hot_ids;
    for (const auto &e : fs::directory_iterator(hot_dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        auto name = e.path().filename().string();
        try {
            hot_ids.emplace_back(name);
        } catch (...) {
            continue;
        }
    }
    std::sort(hot_ids.begin(), hot_ids.end());

    if (hot_ids.empty()) return {};

    auto section_size   = Config::DataStorage::SECTION_SIZE;
    SectionId current   = hot_ids.front();
    SectionId pack_idx  = current / section_size;
    SectionId pack_last = current - SectionId(1); // sentinel

    std::uint64_t total  = hot_ids.size();
    std::uint64_t packed = 0;

    while (true) {
        SectionId pack_first = pack_idx * section_size;
        SectionId boundary   = pack_first + section_size - 1;

        std::map<SectionId, std::string> sections;
        for (SectionId s = pack_first; s <= boundary; s = s + 1) {
            fs::path p = hot_dir / s.to_string();
            std::ifstream f(p, std::ios::binary);
            if (!f) {
                // hot dir doesn't have a full pack worth — keep leftovers as hot
                sections.clear();
                break;
            }
            std::string content((std::istreambuf_iterator<char>(f)), {});
            sections.emplace(s, std::move(content));
        }

        if (sections.empty()) break;

        Pack::PackId pid;
        {
            auto idx_int = pack_idx.to_int();
            if (!idx_int.has_value() || *idx_int < 0) {
                return std::unexpected(Error::PackFailed);
            }
            pid = static_cast<Pack::PackId>(*idx_int);
        }

        fs::path pack_path = fs::path(ChainConst::DAG_PACKS_FOLDER)
                             / (std::to_string(pid) + ".pack");
        auto write = Pack::write(pack_path, pid, sections);
        if (!write.has_value()) return std::unexpected(Error::PackFailed);

        for (SectionId s = pack_first; s <= boundary; s = s + 1) {
            std::error_code rmec;
            fs::remove(hot_dir / s.to_string(), rmec);
        }

        packed += section_size.to_int().value_or(10000);
        if (on_progress) {
            on_progress({ .processed = packed, .total = total, .stage = "packing" });
        }

        pack_idx = pack_idx + 1;
    }

    return {};
}

// Walk dag/cache/BalanceCache.db, rewrite any hex-looking balance strings as decimal.
std::expected<void, Error> migrate_balance_cache() {
    fs::path db_path = ChainConst::BALANCE_CACHE;
    std::error_code ec;
    if (!fs::exists(db_path, ec)) return {};

    DbConnector db(db_path.string());
    if (!db.open()) {
        eWarning("[Migration] Failed to open balance cache");
        return std::unexpected(Error::ReadFailed);
    }

    auto rows = db.select("SELECT actor_id, token_id, balance FROM balance_cache");
    if (rows.empty()) return {};

    int converted = 0;
    db.query("BEGIN TRANSACTION");
    for (const auto &row : rows) {
        auto it = row.find("balance");
        if (it == row.end()) continue;
        const std::string &balance_str = it->second;
        auto decimal = BigNumberFloat::from_hex(balance_str).to_string();

        auto actor_it = row.find("actor_id");
        auto token_it = row.find("token_id");
        if (actor_it == row.end() || token_it == row.end()) continue;

        std::string q = fmt::format(
            "UPDATE balance_cache SET balance = '{}' WHERE actor_id = '{}' AND token_id = '{}'",
            decimal, actor_it->second, token_it->second);
        db.query(q);
        converted++;
    }
    db.query("COMMIT");

    eLog("[Migration] Balance cache: {} rows converted to decimal", converted);
    return {};
}

// Rewrite dag/range: hex SectionRange fields -> decimal.
std::expected<void, Error> migrate_range_file() {
    fs::path range_path = ChainConst::DAG_RANGE_PATH;
    std::ifstream in(range_path);
    if (!in) return {}; // no range file, nothing to do

    std::string content((std::istreambuf_iterator<char>(in)), {});
    in.close();

    auto parsed = Json::deserialize<SectionRange>(content);
    if (!parsed.has_value()) return std::unexpected(Error::ParseFailed);

    auto normalize = [](const std::string &v) {
        if (v.empty()) return std::string("0");
        return BigNumber::from_hex(v).to_string();
    };

    SectionRange decimal {
        .first       = normalize(parsed->first),
        .last        = normalize(parsed->last),
        .last_cached = normalize(parsed->last_cached),
    };

    std::ofstream out(range_path, std::ios::trunc);
    if (!out) return std::unexpected(Error::WriteFailed);
    out << Json::serialize(decimal);
    return {};
}

} // namespace

bool needs_migration() {
    namespace fs = std::filesystem;
    fs::path dag_root = ChainConst::DAG_FOLDER;
    std::error_code ec;
    if (!fs::exists(dag_root, ec)) return false;

    for (const auto &entry : fs::directory_iterator(dag_root, ec)) {
        if (ec) return false;
        if (looks_like_hex_shard_dir(entry.path())) return true;
    }
    return false;
}

std::expected<void, Error> migrate(ProgressCallback on_progress) {
    auto settings = Utils::read_settings();
    if (settings.dag_version.value_or(0) >= CURRENT_DAG_VERSION && !needs_migration()) {
        return std::unexpected(Error::AlreadyCurrent);
    }
    if (!needs_migration()) {
        return std::unexpected(Error::NoWorkNeeded);
    }

    eLog("[Migration] Starting hex -> decimal + pack migration");

    auto s1 = migrate_sections(on_progress);
    if (!s1.has_value()) return s1;

    auto s2 = pack_cold_sections(on_progress);
    if (!s2.has_value()) return s2;

    auto s3 = migrate_range_file();
    if (!s3.has_value()) return s3;

    auto s4 = migrate_balance_cache();
    if (!s4.has_value()) return s4;

    settings.dag_version = CURRENT_DAG_VERSION;
    Utils::write_settings(settings);

    remove_checkpoint();

    if (on_progress) {
        on_progress({ .processed = 0, .total = 0, .stage = "done" });
    }

    eLog("[Migration] Complete");
    return {};
}

} // namespace DagMigration
