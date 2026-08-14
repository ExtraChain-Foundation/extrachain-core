/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "chain/dag_quarantine.h"

#include "chain/transaction.h"
#include "utils/db_connector.h"
#include "utils/exc_utils.h"

namespace {
    constexpr auto QUARANTINE_TABLE = "dag_quarantine";

    std::optional<std::uint64_t> parse_unsigned(const std::string &value) {
        std::uint64_t result    = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (error != std::errc {} || end != value.data() + value.size()) {
            return std::nullopt;
        }
        return result;
    }
} // namespace

DagQuarantine::DagQuarantine(const std::filesystem::path &database_path) {
    std::error_code error;
    std::filesystem::create_directories(database_path.parent_path(), error);
    if (error) {
        return;
    }

    database_ = std::make_unique<DbConnector>(database_path);
    if (!database_->open()
        || !database_->query(
            "CREATE TABLE IF NOT EXISTS dag_quarantine ("
            "transaction_hash TEXT PRIMARY KEY, section_id TEXT NOT NULL, reason TEXT NOT NULL, "
            "source TEXT NOT NULL, first_seen_ms INTEGER NOT NULL, last_seen_ms INTEGER NOT NULL, "
            "attempts INTEGER NOT NULL, status TEXT NOT NULL)")) {
        database_.reset();
    }
}

DagQuarantine::~DagQuarantine() = default;

bool DagQuarantine::record(const Transaction &transaction, const std::string &reason, const std::string &source) {
    if (!database_) {
        return false;
    }

    const auto now = Utils::current_date_ms();
    const auto existing =
        database_->select("SELECT first_seen_ms, attempts FROM dag_quarantine WHERE transaction_hash = ?",
                          QUARANTINE_TABLE,
                          { { "transaction_hash", transaction.hash() } });
    auto first_seen = now;
    auto attempts   = 1ULL;
    if (!existing.empty()) {
        const auto stored_first_seen = parse_unsigned(existing.front().at("first_seen_ms"));
        const auto stored_attempts   = parse_unsigned(existing.front().at("attempts"));
        if (stored_first_seen.has_value()) {
            first_seen = stored_first_seen.value();
        }
        if (stored_attempts.has_value()) {
            attempts = stored_attempts.value() + 1ULL;
        }
    }

    return database_->replace(QUARANTINE_TABLE,
                              { { "transaction_hash", transaction.hash() },
                                { "section_id", transaction.section().to_string() },
                                { "reason", reason },
                                { "source", source },
                                { "first_seen_ms", std::to_string(first_seen) },
                                { "last_seen_ms", std::to_string(now) },
                                { "attempts", std::to_string(attempts) },
                                { "status", "pending" } });
}

bool DagQuarantine::contains(const std::string &transaction_hash) const {
    if (!database_) {
        return false;
    }
    return !database_
                ->select(
                    "SELECT transaction_hash FROM dag_quarantine "
                    "WHERE transaction_hash = ? AND status = 'pending'",
                    QUARANTINE_TABLE,
                    { { "transaction_hash", transaction_hash } })
                .empty();
}

void DagQuarantine::resolve_range(const BigNumber &from, const BigNumber &to) {
    if (!database_) {
        return;
    }
    for (const auto &row :
         database_->select("SELECT transaction_hash, section_id FROM dag_quarantine", QUARANTINE_TABLE)) {
        const auto section = BigNumber::create(row.at("section_id"));
        if (!section.has_value() || section.value() < from || section.value() > to) {
            continue;
        }
        static_cast<void>(database_->update(QUARANTINE_TABLE,
                                            { { "status", "resolved" } },
                                            { { "transaction_hash", row.at("transaction_hash") } }));
    }
}

std::uint64_t DagQuarantine::count() const {
    return database_ ? database_->count(QUARANTINE_TABLE) : 0;
}
