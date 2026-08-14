/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/safety_store.h"

#include <fmt/format.h>

#include "utils/db_connector.h"
#include "utils/exc_utils_base64.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view CreateStateTable =
            "CREATE TABLE IF NOT EXISTS safety_state (slot TEXT PRIMARY KEY, payload TEXT NOT NULL)";
        constexpr std::string_view CreateVoteTable =
            "CREATE TABLE IF NOT EXISTS safety_votes (vote_key TEXT PRIMARY KEY, payload TEXT NOT NULL)";
        constexpr std::string_view CreateProposalTable =
            "CREATE TABLE IF NOT EXISTS consensus_proposals (hash TEXT PRIMARY KEY, height INTEGER NOT NULL, "
            "payload TEXT NOT NULL)";
        constexpr std::string_view CreateCertificateTable =
            "CREATE TABLE IF NOT EXISTS consensus_certificates (hash TEXT PRIMARY KEY, height INTEGER NOT NULL, "
            "payload TEXT NOT NULL)";

        template <typename T>
        std::expected<T, ConsensusError> decode(std::string_view encoded) {
            const auto bytes = Utils::from_base64(std::string(encoded));
            if (!bytes.has_value()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto value = MessagePack::deserialize<T>(bytes.value());
            if (!value.has_value()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            return value.value();
        }

        template <typename T>
        std::string encode(const T& value) {
            return Utils::to_base64(MessagePack::serialize(value));
        }
    } // namespace

    SafetyStore::SafetyStore(std::filesystem::path database_path)
        : database_path_(std::move(database_path)) {
    }

    SafetyStore::~SafetyStore() = default;

    std::expected<void, ConsensusError> SafetyStore::open() {
        std::lock_guard lock(mutex_);
        if (database_ && database_->is_open()) {
            return {};
        }
        std::error_code directory_error;
        if (!database_path_.parent_path().empty()) {
            std::filesystem::create_directories(database_path_.parent_path(), directory_error);
        }
        if (directory_error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        database_ = std::make_unique<DbConnector>(database_path_);
        if (!database_->open()) {
            database_.reset();
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto journal_mode = database_->select("PRAGMA journal_mode=WAL");
        if (journal_mode.size() != 1) {
            database_.reset();
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto journal = journal_mode.front().find("journal_mode");
        if (journal == journal_mode.front().end() || journal->second != "wal"
            || !database_->query("PRAGMA synchronous=FULL") || !database_->query(std::string(CreateStateTable))
            || !database_->query(std::string(CreateVoteTable))
            || !database_->query(std::string(CreateProposalTable))
            || !database_->query(std::string(CreateCertificateTable))) {
            database_.reset();
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto integrity = database_->select("PRAGMA quick_check");
        if (integrity.size() != 1) {
            database_.reset();
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const auto status = integrity.front().find("quick_check");
        if (status == integrity.front().end() || status->second != "ok") {
            database_.reset();
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<std::optional<SafetyState>, ConsensusError> SafetyStore::load_state() {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto rows = database_->select("SELECT payload FROM safety_state WHERE slot = 'active'");
        if (rows.empty()) {
            const auto votes        = database_->select("SELECT vote_key FROM safety_votes LIMIT 1");
            const auto proposals    = database_->select("SELECT hash FROM consensus_proposals LIMIT 1");
            const auto certificates = database_->select("SELECT hash FROM consensus_certificates LIMIT 1");
            if (!votes.empty() || !proposals.empty() || !certificates.empty()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            return std::optional<SafetyState> {};
        }
        const auto payload = rows.front().find("payload");
        if (payload == rows.front().end()) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const auto state = decode<SafetyState>(payload->second);
        if (!state.has_value()) {
            return std::unexpected(state.error());
        }
        return std::optional<SafetyState>(state.value());
    }

    std::expected<bool, ConsensusError> SafetyStore::persist_vote(const Vote& vote, const SafetyState& state) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }

        const auto key = vote_key(vote);
        const auto rows =
            database_->select(fmt::format("SELECT payload FROM safety_votes WHERE vote_key = '{}'", key));
        if (!rows.empty()) {
            const auto payload = rows.front().find("payload");
            if (payload == rows.front().end()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto stored = decode<Vote>(payload->second);
            if (!stored.has_value()) {
                return std::unexpected(stored.error());
            }
            if (stored.value().header_hash != vote.header_hash || stored.value().signature != vote.signature) {
                return std::unexpected(ConsensusError::ConflictingVote);
            }
            return false;
        }

        if (!database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const bool inserted =
            database_->insert("safety_votes", { { "vote_key", key }, { "payload", encode(vote) } });
        const auto stored =
            inserted ? persist_state_unlocked(state)
                     : std::expected<void, ConsensusError>(std::unexpected(ConsensusError::StorageFailure));
        if (!stored.has_value() || !database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return true;
    }

    std::expected<void, ConsensusError> SafetyStore::persist_state(const SafetyState& state) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        if (!database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const auto stored = persist_state_unlocked(state);
        if (!stored.has_value() || !database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<void, ConsensusError> SafetyStore::persist_proposal(const Proposal& proposal) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()
            || !database_->replace("consensus_proposals",
                                   { { "hash", hash_header(proposal.header) },
                                     { "height", std::to_string(proposal.header.height) },
                                     { "payload", encode(proposal) } })) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<void, ConsensusError> SafetyStore::persist_certificate_state(
        const QuorumCertificate& certificate,
        const SafetyState&       state) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open() || !database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const bool certificate_stored = database_->replace("consensus_certificates",
                                                           { { "hash", hash_certificate(certificate) },
                                                             { "height", std::to_string(certificate.height) },
                                                             { "payload", encode(certificate) } });
        const auto state_stored =
            certificate_stored
                ? persist_state_unlocked(state)
                : std::expected<void, ConsensusError>(std::unexpected(ConsensusError::StorageFailure));
        if (!state_stored.has_value() || !database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<std::vector<Proposal>, ConsensusError> SafetyStore::load_proposals(
        std::uint64_t minimum_height) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        std::vector<Proposal> result;
        for (const auto& row : database_->select(
                 fmt::format("SELECT payload FROM consensus_proposals WHERE height >= {}", minimum_height))) {
            const auto payload = row.find("payload");
            if (payload == row.end()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto value = decode<Proposal>(payload->second);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            result.push_back(value.value());
        }
        return result;
    }

    std::expected<std::vector<QuorumCertificate>, ConsensusError> SafetyStore::load_certificates(
        std::uint64_t minimum_height) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        std::vector<QuorumCertificate> result;
        for (const auto& row : database_->select(
                 fmt::format("SELECT payload FROM consensus_certificates WHERE height >= {}", minimum_height))) {
            const auto payload = row.find("payload");
            if (payload == row.end()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto value = decode<QuorumCertificate>(payload->second);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            result.push_back(value.value());
        }
        return result;
    }

    std::string SafetyStore::vote_key(const Vote& vote) {
        return fmt::format("{}:{}:{}:{}:{}",
                           vote.epoch,
                           vote.height,
                           vote.round,
                           std::to_underlying(vote.phase),
                           vote.validator_id);
    }

    std::expected<void, ConsensusError> SafetyStore::persist_state_unlocked(const SafetyState& state) {
        if (!database_->replace("safety_state", { { "slot", "active" }, { "payload", encode(state) } })) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

} // namespace ExtraChain::Consensus
