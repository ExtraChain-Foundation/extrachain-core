/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "chain/dag_recovery.h"

#include <charconv>

#include "utils/db_connector.h"
#include "utils/exc_utils.h"

namespace {
    constexpr auto RecoveryTable = "dag_recovery";

    std::optional<std::uint64_t> parse_unsigned(const std::string &value) {
        std::uint64_t result    = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (error != std::errc {} || end != value.data() + value.size()) {
            return std::nullopt;
        }
        return result;
    }

    std::optional<DagRecoveryIncident> parse_incident(const DbRow &row) {
        const auto first    = SectionId::create(row.at("first_section"));
        const auto last     = SectionId::create(row.at("last_section"));
        const auto seen     = parse_unsigned(row.at("first_seen_ms"));
        const auto updated  = parse_unsigned(row.at("last_seen_ms"));
        const auto attempts = parse_unsigned(row.at("attempts"));
        const auto stage    = parse_unsigned(row.at("stage"));
        if (!first.has_value() || !last.has_value() || !seen.has_value() || !updated.has_value()
            || !attempts.has_value() || !stage.has_value()
            || stage.value() > std::to_underlying(DagRecoveryStage::Failed)) {
            return std::nullopt;
        }
        return DagRecoveryIncident {
            .id            = row.at("incident_id"),
            .first_section = first.value(),
            .last_section  = last.value(),
            .reason        = row.at("reason"),
            .source        = row.at("source"),
            .observed_root = row.at("observed_root"),
            .expected_root = row.at("expected_root"),
            .proof_hash    = row.at("proof_hash"),
            .first_seen_ms = seen.value(),
            .last_seen_ms  = updated.value(),
            .attempts      = attempts.value(),
            .stage         = static_cast<DagRecoveryStage>(stage.value()),
        };
    }
} // namespace

DagRecoveryJournal::DagRecoveryJournal(const std::filesystem::path &database_path) {
    std::error_code error;
    std::filesystem::create_directories(database_path.parent_path(), error);
    if (error) {
        return;
    }
    database_ = std::make_unique<DbConnector>(database_path);
    if (!database_->open()
        || !database_->query(
            "CREATE TABLE IF NOT EXISTS dag_recovery ("
            "incident_id TEXT PRIMARY KEY, first_section TEXT NOT NULL, last_section TEXT NOT NULL, "
            "reason TEXT NOT NULL, source TEXT NOT NULL, observed_root TEXT NOT NULL, "
            "expected_root TEXT NOT NULL, proof_hash TEXT NOT NULL, first_seen_ms INTEGER NOT NULL, "
            "last_seen_ms INTEGER NOT NULL, attempts INTEGER NOT NULL, stage INTEGER NOT NULL)")) {
        database_.reset();
    }
}

DagRecoveryJournal::~DagRecoveryJournal() = default;

bool DagRecoveryJournal::available() const {
    std::lock_guard lock(mutex_);
    return database_ != nullptr && database_->is_open();
}

std::optional<std::string> DagRecoveryJournal::record(const SectionId   &first,
                                                      const SectionId   &last,
                                                      const std::string &reason,
                                                      const std::string &source,
                                                      const std::string &observed_root) {
    std::lock_guard lock(mutex_);
    if (!database_ || first < SectionId(0) || last < first || reason.empty() || source.empty()) {
        return std::nullopt;
    }
    const auto id = Utils::calculate_hash("EXC_DAG_RECOVERY_V1" + first.to_string() + ':' + last.to_string() + ':'
                                              + reason + ':' + source,
                                          Utils::HashAlgorithm::Blake3);
    const auto existing = database_->select(
        "SELECT first_seen_ms, attempts, observed_root, expected_root, proof_hash, stage "
        "FROM dag_recovery WHERE incident_id = ?",
        RecoveryTable,
        { { "incident_id", id } });
    const auto now        = Utils::current_date_ms();
    auto       first_seen = now;
    auto       attempts   = 1ULL;
    if (!existing.empty()) {
        const auto stored_first_seen = parse_unsigned(existing.front().at("first_seen_ms"));
        const auto stored_attempts   = parse_unsigned(existing.front().at("attempts"));
        if (stored_first_seen.has_value()) {
            first_seen = stored_first_seen.value();
        }
        if (stored_attempts.has_value()) {
            attempts = stored_attempts.value() + 1;
        }
        const auto stored_stage = parse_unsigned(existing.front().at("stage"));
        if (stored_stage.has_value() && stored_stage.value() != std::to_underlying(DagRecoveryStage::Resolved)) {
            DbRow values { { "last_seen_ms", std::to_string(now) }, { "attempts", std::to_string(attempts) } };
            if (!observed_root.empty()) {
                values.insert_or_assign("observed_root", observed_root);
            }
            if (!database_->update(RecoveryTable, values, { { "incident_id", id } })) {
                return std::nullopt;
            }
            return id;
        }
    }
    if (!database_->replace(RecoveryTable,
                            { { "incident_id", id },
                              { "first_section", first.to_string() },
                              { "last_section", last.to_string() },
                              { "reason", reason },
                              { "source", source },
                              { "observed_root", observed_root },
                              { "expected_root", "" },
                              { "proof_hash", "" },
                              { "first_seen_ms", std::to_string(first_seen) },
                              { "last_seen_ms", std::to_string(now) },
                              { "attempts", std::to_string(attempts) },
                              { "stage", std::to_string(std::to_underlying(DagRecoveryStage::Detected)) } })) {
        return std::nullopt;
    }
    return id;
}

bool DagRecoveryJournal::advance(const std::string &incident_id,
                                 DagRecoveryStage   stage,
                                 const std::string &expected_root,
                                 const std::string &proof_hash) {
    std::lock_guard lock(mutex_);
    if (!database_ || incident_id.empty()) {
        return false;
    }
    const auto rows = database_->select("SELECT stage FROM dag_recovery WHERE incident_id = ?",
                                        RecoveryTable,
                                        { { "incident_id", incident_id } });
    if (rows.size() != 1) {
        return false;
    }
    const auto current = parse_unsigned(rows.front().at("stage"));
    if (!current.has_value() || current.value() > std::to_underlying(DagRecoveryStage::Failed)) {
        return false;
    }
    const auto current_stage = static_cast<DagRecoveryStage>(current.value());
    if (current_stage == DagRecoveryStage::Resolved || current_stage == DagRecoveryStage::Failed) {
        return current_stage == stage;
    }
    if (stage != DagRecoveryStage::Failed && std::to_underlying(stage) < std::to_underlying(current_stage)) {
        return true;
    }
    DbRow values { { "stage", std::to_string(std::to_underlying(stage)) },
                   { "last_seen_ms", std::to_string(Utils::current_date_ms()) } };
    if (!expected_root.empty()) {
        values.insert_or_assign("expected_root", expected_root);
    }
    if (!proof_hash.empty()) {
        values.insert_or_assign("proof_hash", proof_hash);
    }
    return database_->update(RecoveryTable, values, { { "incident_id", incident_id } });
}

std::vector<DagRecoveryIncident> DagRecoveryJournal::select_pending() const {
    std::vector<DagRecoveryIncident> incidents;
    if (!database_) {
        return incidents;
    }
    const auto rows =
        database_->select("SELECT * FROM dag_recovery WHERE stage != ? ORDER BY first_seen_ms ASC",
                          RecoveryTable,
                          { { "stage", std::to_string(std::to_underlying(DagRecoveryStage::Resolved)) } });
    incidents.reserve(rows.size());
    for (const auto &row : rows) {
        try {
            auto incident = parse_incident(row);
            if (incident.has_value()) {
                incidents.push_back(std::move(incident.value()));
            }
        } catch (const std::out_of_range &error) {
            eWarning("[DagRecovery] Invalid journal row: {}", error.what());
        }
    }
    return incidents;
}

std::vector<DagRecoveryIncident> DagRecoveryJournal::pending() const {
    std::lock_guard lock(mutex_);
    return select_pending();
}

std::vector<DagRecoveryIncident> DagRecoveryJournal::pending_in_range(const SectionId &first,
                                                                      const SectionId &last) const {
    std::lock_guard lock(mutex_);
    auto            incidents = select_pending();
    std::erase_if(incidents, [&](const auto &incident) {
        return incident.last_section < first || incident.first_section > last;
    });
    return incidents;
}

std::uint64_t DagRecoveryJournal::pending_count() const {
    std::lock_guard lock(mutex_);
    return select_pending().size();
}
