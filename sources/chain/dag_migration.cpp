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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>
#include <vector>

#include "chain/dag.h" // for SectionRange
#include "chain/pack.h"
#include "chain/pack_registry.h"
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

// Section id as a full 64-bit value. SectionId::to_int() is a 32-bit int, so
// going through it would truncate ids past 2^31 and corrupt the resume point;
// parse the decimal string directly instead.
std::uint64_t section_to_u64(const SectionId &id) {
    try {
        return std::stoull(id.to_string());
    } catch (...) {
        return 0;
    }
}

fs::path checkpoint_path(const fs::path &dag_root) {
    return dag_root / "migration.progress";
}

std::optional<Checkpoint> read_checkpoint(const fs::path &dag_root) {
    std::ifstream f(checkpoint_path(dag_root));
    if (!f) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(f)), {});
    auto parsed = Json::deserialize<Checkpoint>(content);
    if (!parsed.has_value()) return std::nullopt;
    return *parsed;
}

bool write_checkpoint(const fs::path &dag_root, const Checkpoint &c) {
    std::ofstream f(checkpoint_path(dag_root));
    if (!f) return false;
    f << Json::serialize(c);
    return static_cast<bool>(f);
}

void remove_checkpoint(const fs::path &dag_root) {
    std::error_code ec;
    fs::remove(checkpoint_path(dag_root), ec);
}

// Non-empty string of only hex digits (both shard-dir and section-filename
// detection below rely on the same predicate).
bool is_all_hex(const std::string &s) {
    if (s.empty()) return false;
    for (char c : s) {
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) return false;
    }
    return true;
}

// Detect whether a directory name is a hex (shard) rather than decimal.
// Old layout used hex-named shard folders; new uses "hot"/"packs"/"cache".
bool looks_like_hex_shard_dir(const fs::path &p) {
    std::error_code ec;
    if (!fs::is_directory(p, ec) || ec) return false;
    auto name = p.filename().string();
    if (name == "hot" || name == "packs" || name == "cache") return false;
    return is_all_hex(name);
}

// A legacy section filename is the section id encoded in hex (no ".json" suffix).
std::optional<SectionId> parse_legacy_section_filename(const std::string &name) {
    if (!is_all_hex(name)) return std::nullopt;
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

std::expected<void, Error> collect_legacy_sections(const fs::path                              &dag_root,
                                                   std::vector<std::pair<SectionId, fs::path>> &out) {
    std::error_code ec;
    if (!fs::exists(dag_root, ec))
        return ec ? std::unexpected(Error::ReadFailed) : std::unexpected(Error::DagNotFound);

    for (const auto &shard_entry : fs::directory_iterator(dag_root, ec)) {
        if (ec)
            return std::unexpected(Error::ReadFailed);
        if (!looks_like_hex_shard_dir(shard_entry.path()))
            continue;

        std::error_code shard_ec;
        for (const auto &file_entry : fs::directory_iterator(shard_entry.path(), shard_ec)) {
            if (shard_ec)
                return std::unexpected(Error::ReadFailed);
            std::error_code status_ec;
            if (!file_entry.is_regular_file(status_ec)) {
                if (status_ec)
                    return std::unexpected(Error::ReadFailed);
                continue;
            }
            auto name = file_entry.path().filename().string();
            if (name.starts_with("."))
                continue;

            auto sid = parse_legacy_section_filename(name);
            if (!sid.has_value())
                continue;
            out.emplace_back(*sid, file_entry.path());
        }
        if (shard_ec)
            return std::unexpected(Error::ReadFailed);
    }
    if (ec)
        return std::unexpected(Error::ReadFailed);

    return {};
}

std::expected<void, Error> migrate_sections(const fs::path &dag_root, ProgressCallback on_progress) {
    std::vector<std::pair<SectionId, fs::path>> sections;
    auto                                        collected = collect_legacy_sections(dag_root, sections);
    if (!collected.has_value())
        return std::unexpected(collected.error());

    std::sort(sections.begin(), sections.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    if (sections.empty()) return {}; // no legacy data

    const auto hot_dir   = dag_root / "hot";
    const auto packs_dir = dag_root / "packs";
    fs::create_directories(hot_dir);
    fs::create_directories(packs_dir);

    // Resume support
    auto      cp      = read_checkpoint(dag_root);
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
        fs::path    dest         = hot_dir / decimal_name;

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
                .last_section_done = section_to_u64(sid),
                .stage             = "sections",
            };
            if (!write_checkpoint(dag_root, ncp)) {
                return std::unexpected(Error::CheckpointFailed);
            }
        }
    }

    // Clean up now-empty legacy shard dirs
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(dag_root, ec)) {
        if (ec) break;
        if (!looks_like_hex_shard_dir(entry.path())) continue;
        std::error_code rmec;
        fs::remove_all(entry.path(), rmec);
    }

    return {};
}

// Pack consecutive ranges of SECTION_SIZE from hot/ into packs/.
std::expected<void, Error> pack_cold_sections(const fs::path &dag_root, ProgressCallback on_progress) {
    fs::path        hot_dir = dag_root / "hot";
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

        fs::path pack_path = dag_root / "packs" / (std::to_string(pid) + ".pack");
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
std::expected<void, Error> migrate_balance_cache(const fs::path &dag_root) {
    fs::path        db_path = dag_root / "cache" / "BalanceCache.db";
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
std::expected<void, Error> migrate_range_file(const fs::path &dag_root) {
    fs::path      range_path = dag_root / "range";
    std::ifstream in(range_path);
    if (!in) return {}; // no range file, nothing to do

    std::string content((std::istreambuf_iterator<char>(in)), {});
    in.close();

    auto parsed = Json::deserialize<SectionRange>(content);
    if (!parsed.has_value()) return std::unexpected(Error::ParseFailed);

    auto normalize = [](const std::string &v) {
        if (v.empty())
            return std::string("0");
        return BigNumber::from_hex(v).to_string();
    };

    SectionRange decimal {
        .first       = normalize(parsed->first),
        .last        = normalize(parsed->last),
        .last_cached = normalize(parsed->last_cached),
    };

    std::ofstream out(range_path, std::ios::trunc);
    if (!out)
        return std::unexpected(Error::WriteFailed);
    out << Json::serialize(decimal);
    return out ? std::expected<void, Error> {} : std::unexpected(Error::WriteFailed);
}

bool needs_migration_at(const fs::path &dag_root) {
    std::error_code ec;
    if (!fs::exists(dag_root, ec))
        return static_cast<bool>(ec);

    for (const auto &entry : fs::directory_iterator(dag_root, ec)) {
        if (ec)
            return true;
        if (looks_like_hex_shard_dir(entry.path()))
            return true;
    }
    return static_cast<bool>(ec);
}

bool has_activation_marker(const fs::path &dag_root) {
    std::error_code ec;
    return fs::exists(dag_root / "copy.complete", ec) || fs::exists(dag_root / "migration.ready", ec);
}

std::expected<void, Error> validate_staging(const fs::path                                    &dag_root,
                                            const std::vector<std::pair<SectionId, fs::path>> &expected_sections,
                                            bool                                               expected_range) {
    if (needs_migration_at(dag_root))
        return std::unexpected(Error::ValidationFailed);

    Pack::Registry packs(dag_root / "packs");
    packs.rescan();

    for (const auto &[section_id, unused_source] : expected_sections) {
        (void)unused_source;
        std::optional<std::string> payload;
        const auto                 hot_path = dag_root / "hot" / section_id.to_string();
        std::ifstream              hot(hot_path, std::ios::binary);
        if (hot) {
            payload = std::string((std::istreambuf_iterator<char>(hot)), {});
        } else {
            payload = packs.read_section(section_id);
        }
        if (!payload.has_value())
            return std::unexpected(Error::ValidationFailed);

        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        auto              section = Json::deserialize<Section>(*payload);
        // `id` is deliberately not part of the serialised form
        // (BOOST_DESCRIBE_STRUCT(Section, (), (transactions, control))): a section
        // knows its number from where it is stored, not from its payload. Every reader
        // therefore assigns it after deserializing, exactly as Dag::read_section does
        // on all three of its paths. Comparing it before assigning it made this check
        // fail on the very first section of every migration.
        if (!section.has_value()) {
            return std::unexpected(Error::ValidationFailed);
        }
        section->id = section_id;
        const bool wrong_transaction = std::ranges::any_of(section->transactions, [&](const Transaction &tx) {
            return tx.section() != section_id;
        });
        if (wrong_transaction)
            return std::unexpected(Error::ValidationFailed);
    }

    const auto range_path = dag_root / "range";
    if (expected_range) {
        std::ifstream range_file(range_path);
        if (!range_file)
            return std::unexpected(Error::ValidationFailed);
        const std::string content((std::istreambuf_iterator<char>(range_file)), {});
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        if (!Json::deserialize<SectionRange>(content).has_value()) {
            return std::unexpected(Error::ValidationFailed);
        }
    }

    return {};
}

std::expected<void, Error> activate_staging(const fs::path &live_root,
                                            const fs::path &staging_root,
                                            const fs::path &backup_root) {
    std::error_code ec;
    if (fs::exists(backup_root, ec))
        return std::unexpected(Error::BackupExists);

    fs::rename(live_root, backup_root, ec);
    if (ec)
        return std::unexpected(Error::ActivationFailed);

    fs::rename(staging_root, live_root, ec);
    if (!ec)
        return {};

    std::error_code restore_error;
    fs::rename(backup_root, live_root, restore_error);
    return std::unexpected(Error::ActivationFailed);
}

} // namespace

bool needs_migration() {
    const fs::path  live_root    = ChainConst::DAG_FOLDER;
    const fs::path  staging_root = live_root.string() + ".migration-staging";
    const fs::path  backup_root  = live_root.string() + ".legacy-backup";
    std::error_code ec;
    return needs_migration_at(live_root) || fs::exists(staging_root, ec)
           || (!fs::exists(live_root, ec) && fs::exists(backup_root, ec)) || has_activation_marker(live_root);
}

std::expected<void, Error> migrate(ProgressCallback on_progress) {
    auto settings = Utils::read_settings();
    if (settings.dag_version.value_or(0) >= CURRENT_DAG_VERSION && !needs_migration()) {
        return std::unexpected(Error::AlreadyCurrent);
    }
    if (!needs_migration()) {
        return std::unexpected(Error::NoWorkNeeded);
    }

    const fs::path live_root    = ChainConst::DAG_FOLDER;
    const fs::path staging_root = live_root.string() + ".migration-staging";
    const fs::path backup_root  = live_root.string() + ".legacy-backup";
    const fs::path copy_path    = staging_root / "copy.complete";
    const fs::path ready_path   = staging_root / "migration.ready";

    std::error_code ec;
    bool            live_exists    = fs::exists(live_root, ec);
    bool            staging_exists = fs::exists(staging_root, ec);
    bool            backup_exists  = fs::exists(backup_root, ec);

    if (settings.dag_version.value_or(0) >= CURRENT_DAG_VERSION && live_exists && !needs_migration_at(live_root)
        && !has_activation_marker(live_root)) {
        if (staging_exists) {
            fs::remove_all(staging_root, ec);
            if (ec)
                return std::unexpected(Error::CopyFailed);
        }
        return std::unexpected(Error::AlreadyCurrent);
    }

    // Recover a stop after the live directory was renamed but before a staging
    // directory was available for activation.
    if (!live_exists && backup_exists && !staging_exists) {
        fs::rename(backup_root, live_root, ec);
        if (ec)
            return std::unexpected(Error::ActivationFailed);
        live_exists   = true;
        backup_exists = false;
    }

    // Recover a stop after staging was activated but before settings and marker
    // updates completed. The backup defines the expected set of sections.
    if (live_exists && backup_exists && !needs_migration_at(live_root)
        && (has_activation_marker(live_root) || settings.dag_version.value_or(0) < CURRENT_DAG_VERSION)) {
        std::vector<std::pair<SectionId, fs::path>> expected_sections;
        auto collected = collect_legacy_sections(backup_root, expected_sections);
        if (!collected.has_value())
            return collected;
        auto valid = validate_staging(live_root, expected_sections, fs::exists(backup_root / "range"));
        if (!valid.has_value())
            return valid;

        settings.dag_version = CURRENT_DAG_VERSION;
        if (!Utils::write_settings(settings))
            return std::unexpected(Error::WriteFailed);
        fs::remove(live_root / "migration.ready", ec);
        fs::remove(live_root / "copy.complete", ec);
        if (on_progress)
            on_progress({ .processed = 0, .total = 0, .stage = "done" });
        eLog("[Migration] Recovered activated storage; legacy data retained at {}", backup_root.string());
        return {};
    }

    const fs::path source_root = live_exists ? live_root : backup_root;
    if (!fs::exists(source_root, ec))
        return std::unexpected(Error::DagNotFound);

    std::vector<std::pair<SectionId, fs::path>> expected_sections;
    auto collected = collect_legacy_sections(source_root, expected_sections);
    if (!collected.has_value())
        return collected;
    const bool expected_range = fs::exists(source_root / "range");

    if (fs::exists(staging_root, ec) && !fs::exists(copy_path, ec)) {
        fs::remove_all(staging_root, ec);
        if (ec)
            return std::unexpected(Error::CopyFailed);
    } else if (fs::exists(staging_root, ec) && !needs_migration_at(staging_root) && !fs::exists(ready_path, ec)) {
        fs::remove_all(staging_root, ec);
        if (ec)
            return std::unexpected(Error::CopyFailed);
    }
    if (!fs::exists(staging_root, ec)) {
        fs::copy(source_root, staging_root, fs::copy_options::recursive, ec);
        if (ec)
            return std::unexpected(Error::CopyFailed);
        std::ofstream copied(copy_path, std::ios::trunc);
        copied << "complete";
        if (!copied)
            return std::unexpected(Error::CopyFailed);
    }

    eLog("[Migration] Starting staged hex -> decimal + pack migration");

    if (!fs::exists(ready_path, ec)) {
        if (needs_migration_at(staging_root)) {
            auto s1 = migrate_sections(staging_root, on_progress);
            if (!s1.has_value())
                return s1;
        }

        auto s2 = pack_cold_sections(staging_root, on_progress);
        if (!s2.has_value())
            return s2;

        auto s3 = migrate_range_file(staging_root);
        if (!s3.has_value())
            return s3;

        auto s4 = migrate_balance_cache(staging_root);
        if (!s4.has_value())
            return s4;

        remove_checkpoint(staging_root);
        std::ofstream ready(ready_path, std::ios::trunc);
        ready << CURRENT_DAG_VERSION;
        if (!ready)
            return std::unexpected(Error::WriteFailed);
    }

    auto valid = validate_staging(staging_root, expected_sections, expected_range);
    if (!valid.has_value())
        return valid;

    if (live_exists) {
        auto activated = activate_staging(live_root, staging_root, backup_root);
        if (!activated.has_value())
            return activated;
    } else {
        fs::rename(staging_root, live_root, ec);
        if (ec)
            return std::unexpected(Error::ActivationFailed);
    }

    settings.dag_version = CURRENT_DAG_VERSION;
    if (!Utils::write_settings(settings)) {
        std::error_code rollback_error;
        fs::rename(live_root, staging_root, rollback_error);
        if (!rollback_error)
            fs::rename(backup_root, live_root, rollback_error);
        return std::unexpected(Error::WriteFailed);
    }
    fs::remove(live_root / "migration.ready", ec);
    fs::remove(live_root / "copy.complete", ec);

    if (on_progress) {
        on_progress({ .processed = 0, .total = 0, .stage = "done" });
    }

    eLog("[Migration] Complete; legacy data retained at {}", backup_root.string());
    return {};
}

} // namespace DagMigration
