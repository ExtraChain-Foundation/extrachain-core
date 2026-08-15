/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/intent_store.h"

#include <algorithm>
#include <charconv>
#include <limits>

#include "utils/db_connector.h"
#include "utils/exc_utils_base64.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view CreateIntentTable =
            "CREATE TABLE IF NOT EXISTS consensus_intents (hash TEXT PRIMARY KEY, sender TEXT NOT NULL, "
            "nonce INTEGER NOT NULL, expires_height INTEGER NOT NULL, payload TEXT NOT NULL)";
        constexpr std::string_view CreateSenderNonceIndex =
            "CREATE UNIQUE INDEX IF NOT EXISTS consensus_intents_sender_nonce "
            "ON consensus_intents(sender, nonce)";
        constexpr std::string_view CreateExpiryIndex =
            "CREATE INDEX IF NOT EXISTS consensus_intents_expiry ON consensus_intents(expires_height)";
        constexpr std::string_view CreateReceiptTable =
            "CREATE TABLE IF NOT EXISTS consensus_intent_receipts (hash TEXT PRIMARY KEY, payload TEXT NOT NULL)";
        constexpr std::string_view CreateNonceTable =
            "CREATE TABLE IF NOT EXISTS consensus_intent_nonces (sender TEXT PRIMARY KEY, nonce INTEGER NOT NULL)";

        template <typename T>
        std::string encode(const T& value) {
            return Utils::to_base64(MessagePack::serialize(value));
        }

        template <typename T>
        std::expected<T, ConsensusError> decode(std::string_view encoded) {
            const auto bytes = Utils::from_base64(std::string(encoded));
            if (!bytes.has_value()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto value = MessagePack::deserialize<T>(bytes.value());
            return value.has_value() ? std::expected<T, ConsensusError>(std::move(value.value()))
                                     : std::unexpected(ConsensusError::StorageFailure);
        }
    } // namespace

    IntentStore::IntentStore(std::filesystem::path database_path)
        : database_path_(std::move(database_path)) {
    }

    IntentStore::~IntentStore() = default;

    std::expected<void, ConsensusError> IntentStore::open() {
        std::lock_guard lock(mutex_);
        if (database_ && database_->is_open()) {
            return {};
        }
        std::error_code error;
        if (!database_path_.parent_path().empty()) {
            std::filesystem::create_directories(database_path_.parent_path(), error);
        }
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        database_ = std::make_unique<DbConnector>(database_path_);
        if (!database_->open()) {
            database_.reset();
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto journal_mode = database_->select("PRAGMA journal_mode=WAL");
        if (journal_mode.size() != 1 || !journal_mode.front().contains("journal_mode")
            || journal_mode.front().at("journal_mode") != "wal" || !database_->query("PRAGMA synchronous=FULL")
            || !database_->query(std::string(CreateIntentTable))
            || !database_->query(std::string(CreateSenderNonceIndex))
            || !database_->query(std::string(CreateExpiryIndex))
            || !database_->query(std::string(CreateReceiptTable))
            || !database_->query(std::string(CreateNonceTable))) {
            database_.reset();
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto integrity = database_->select("PRAGMA quick_check");
        if (integrity.size() != 1 || !integrity.front().contains("quick_check")
            || integrity.front().at("quick_check") != "ok") {
            database_.reset();
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<void, ConsensusError> IntentStore::put(const IntentEnvelope& envelope) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto     hash            = hash_intent(envelope.intent);
        constexpr auto maximum_integer = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (envelope.intent.account_nonce > maximum_integer
            || envelope.intent.expires_after_height > maximum_integer) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }
        if (!database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const auto fail = [&](ConsensusError error) -> std::expected<void, ConsensusError> {
            database_->query("ROLLBACK");
            return std::unexpected(error);
        };
        const auto encoded    = encode(envelope);
        const auto hash_match = database_->select("SELECT hash, payload FROM consensus_intents WHERE hash = ?",
                                                  "consensus_intents",
                                                  { { "hash", hash } });
        if (!hash_match.empty()) {
            if (hash_match.size() != 1 || !hash_match.front().contains("payload")
                || hash_match.front().at("payload") != encoded) {
                return fail(ConsensusError::DuplicateIntent);
            }
            const auto receipts = database_->select("SELECT payload FROM consensus_intent_receipts WHERE hash = ?",
                                                    "consensus_intent_receipts",
                                                    { { "hash", hash } });
            const IntentReceipt accepted { .intent_hash = hash, .status = IntentStatus::Accepted };
            if ((!receipts.empty()
                 && (receipts.size() != 1 || !receipts.front().contains("payload")
                     || receipts.front().at("payload") != encode(accepted)))
                || (receipts.empty()
                    && !database_->replace("consensus_intent_receipts",
                                           { { "hash", hash }, { "payload", encode(accepted) } }))) {
                return fail(ConsensusError::StorageFailure);
            }
            if (!database_->query("COMMIT")) {
                return fail(ConsensusError::StorageFailure);
            }
            return {};
        }
        const auto sender_matches = database_->select("SELECT hash, nonce FROM consensus_intents WHERE sender = ?",
                                                      "consensus_intents",
                                                      { { "sender", envelope.intent.sender.to_string() } });
        const auto nonce          = std::to_string(envelope.intent.account_nonce);
        if (std::ranges::any_of(sender_matches, [&nonce](const DbRow& row) {
                return row.contains("nonce") && row.at("nonce") == nonce;
            })) {
            return fail(ConsensusError::DuplicateIntent);
        }
        const IntentReceipt accepted { .intent_hash = hash, .status = IntentStatus::Accepted };
        if (!database_->insert("consensus_intents",
                               { { "hash", hash },
                                 { "sender", envelope.intent.sender.to_string() },
                                 { "nonce", std::to_string(envelope.intent.account_nonce) },
                                 { "expires_height", std::to_string(envelope.intent.expires_after_height) },
                                 { "payload", encoded } })
            || !database_->replace("consensus_intent_receipts",
                                   { { "hash", hash }, { "payload", encode(accepted) } })
            || !database_->query("COMMIT")) {
            return fail(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<void, ConsensusError> IntentStore::erase(const std::vector<std::string>& intent_hashes) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        if (!database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        for (const auto& hash : intent_hashes) {
            if (!database_->delete_row("consensus_intents", { { "hash", hash } })) {
                database_->query("ROLLBACK");
                return std::unexpected(ConsensusError::StorageFailure);
            }
        }
        if (!database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<void, ConsensusError> IntentStore::expire(const std::vector<std::string>& intent_hashes) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        if (intent_hashes.empty()) {
            return {};
        }
        if (!database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        for (const auto& hash : intent_hashes) {
            const IntentReceipt receipt {
                .intent_hash = hash,
                .status      = IntentStatus::Expired,
                .error       = ConsensusError::IntentExpired,
            };
            if (!database_->replace("consensus_intent_receipts",
                                    { { "hash", hash }, { "payload", encode(receipt) } })
                || !database_->delete_row("consensus_intents", { { "hash", hash } })) {
                database_->query("ROLLBACK");
                return std::unexpected(ConsensusError::StorageFailure);
            }
        }
        if (!database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<std::vector<IntentEnvelope>, ConsensusError> IntentStore::load_pending() {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        std::vector<IntentEnvelope> result;
        const auto                  rows =
            database_->select("SELECT payload FROM consensus_intents ORDER BY sender ASC, nonce ASC");
        result.reserve(rows.size());
        for (const auto& row : rows) {
            if (!row.contains("payload")) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            auto envelope = decode<IntentEnvelope>(row.at("payload"));
            if (!envelope.has_value()) {
                return std::unexpected(envelope.error());
            }
            result.push_back(std::move(envelope.value()));
        }
        return result;
    }

    std::expected<std::map<ActorId, std::uint64_t>, ConsensusError> IntentStore::load_committed_nonces() {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        std::map<ActorId, std::uint64_t> result;
        for (const auto& row : database_->select("SELECT sender, nonce FROM consensus_intent_nonces")) {
            if (!row.contains("sender") || !row.contains("nonce")) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto    sender     = ActorId::create(row.at("sender"));
            std::uint64_t nonce      = 0;
            const auto&   nonce_text = row.at("nonce");
            const auto parsed = std::from_chars(nonce_text.data(), nonce_text.data() + nonce_text.size(), nonce);
            if (!sender.has_value() || parsed.ec != std::errc {}
                || parsed.ptr != nonce_text.data() + nonce_text.size()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            result.emplace(sender.value(), nonce);
        }
        return result;
    }

    std::expected<void, ConsensusError> IntentStore::commit_finalized(
        const std::vector<std::pair<IntentEnvelope, IntentReceipt>>& finalized) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        if (finalized.empty()) {
            return {};
        }
        std::map<ActorId, std::uint64_t> committed_nonces;
        for (const auto& row : database_->select("SELECT sender, nonce FROM consensus_intent_nonces")) {
            if (!row.contains("sender") || !row.contains("nonce")) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto    sender     = ActorId::create(row.at("sender"));
            std::uint64_t nonce      = 0;
            const auto&   nonce_text = row.at("nonce");
            const auto parsed = std::from_chars(nonce_text.data(), nonce_text.data() + nonce_text.size(), nonce);
            if (!sender.has_value() || parsed.ec != std::errc {}
                || parsed.ptr != nonce_text.data() + nonce_text.size()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            committed_nonces.emplace(sender.value(), nonce);
        }
        if (!database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        for (const auto& [envelope, receipt] : finalized) {
            const auto hash    = hash_intent(envelope.intent);
            const auto pending = database_->select("SELECT payload FROM consensus_intents WHERE hash = ?",
                                                   "consensus_intents",
                                                   { { "hash", hash } });
            if (envelope.intent.account_nonce <= committed_nonces[envelope.intent.sender]) {
                const auto stored_receipt =
                    database_->select("SELECT payload FROM consensus_intent_receipts WHERE hash = ?",
                                      "consensus_intent_receipts",
                                      { { "hash", hash } });
                if (!pending.empty() || stored_receipt.size() != 1 || !stored_receipt.front().contains("payload")
                    || stored_receipt.front().at("payload") != encode(receipt)) {
                    database_->query("ROLLBACK");
                    return std::unexpected(ConsensusError::StorageFailure);
                }
                continue;
            }
            if (receipt.status != IntentStatus::Finalized || receipt.intent_hash != hash
                || envelope.intent.account_nonce != committed_nonces[envelope.intent.sender] + 1
                || pending.size() != 1 || !pending.front().contains("payload")
                || pending.front().at("payload") != encode(envelope)
                || !database_->replace("consensus_intent_receipts",
                                       { { "hash", hash }, { "payload", encode(receipt) } })
                || !database_->replace("consensus_intent_nonces",
                                       { { "sender", envelope.intent.sender.to_string() },
                                         { "nonce", std::to_string(envelope.intent.account_nonce) } })
                || !database_->delete_row("consensus_intents", { { "hash", hash } })) {
                database_->query("ROLLBACK");
                return std::unexpected(ConsensusError::StorageFailure);
            }
            committed_nonces[envelope.intent.sender] = envelope.intent.account_nonce;
        }
        if (!database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<std::optional<IntentReceipt>, ConsensusError> IntentStore::receipt(
        std::string_view intent_hash) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto rows = database_->select("SELECT payload FROM consensus_intent_receipts WHERE hash = ?",
                                            "consensus_intent_receipts",
                                            { { "hash", std::string(intent_hash) } });
        if (rows.empty()) {
            return std::optional<IntentReceipt> {};
        }
        if (rows.size() != 1 || !rows.front().contains("payload")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        auto value = decode<IntentReceipt>(rows.front().at("payload"));
        return value.has_value()
                   ? std::expected<std::optional<IntentReceipt>, ConsensusError>(std::move(value.value()))
                   : std::unexpected(value.error());
    }

} // namespace ExtraChain::Consensus
