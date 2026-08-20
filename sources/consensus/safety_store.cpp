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
        constexpr std::string_view CreateTimeoutVoteTable =
            "CREATE TABLE IF NOT EXISTS safety_timeout_votes (vote_key TEXT PRIMARY KEY, payload TEXT NOT NULL)";
        constexpr std::string_view CreateProposalTable =
            "CREATE TABLE IF NOT EXISTS consensus_proposals (hash TEXT PRIMARY KEY, height INTEGER NOT NULL, "
            "payload TEXT NOT NULL)";
        constexpr std::string_view CreateBatchTable =
            "CREATE TABLE IF NOT EXISTS consensus_batches (hash TEXT PRIMARY KEY, height INTEGER NOT NULL, "
            "payload TEXT NOT NULL)";
        constexpr std::string_view CreateCertificateTable =
            "CREATE TABLE IF NOT EXISTS consensus_certificates (hash TEXT PRIMARY KEY, height INTEGER NOT NULL, "
            "payload TEXT NOT NULL)";
        constexpr std::string_view CreateTimeoutCertificateTable =
            "CREATE TABLE IF NOT EXISTS consensus_timeout_certificates (hash TEXT PRIMARY KEY, "
            "height INTEGER NOT NULL, payload TEXT NOT NULL)";
        constexpr std::string_view CreateFinalityProofTable =
            "CREATE TABLE IF NOT EXISTS consensus_finality_proofs (height INTEGER PRIMARY KEY, "
            "first_section INTEGER NOT NULL, last_section INTEGER NOT NULL, "
            "finalized_hash TEXT NOT NULL, child_hash TEXT NOT NULL, grandchild_hash TEXT NOT NULL, "
            "decision_certificate_hash TEXT NOT NULL)";
        constexpr std::string_view CreateProposalHeightIndex =
            "CREATE INDEX IF NOT EXISTS consensus_proposals_height ON consensus_proposals(height)";
        constexpr std::string_view CreateBatchHeightIndex =
            "CREATE INDEX IF NOT EXISTS consensus_batches_height ON consensus_batches(height)";
        constexpr std::string_view CreateCertificateHeightIndex =
            "CREATE INDEX IF NOT EXISTS consensus_certificates_height ON consensus_certificates(height)";
        constexpr std::string_view CreateTimeoutCertificateHeightIndex =
            "CREATE INDEX IF NOT EXISTS consensus_timeout_certificates_height "
            "ON consensus_timeout_certificates(height)";
        constexpr std::string_view CreateFinalitySectionIndex =
            "CREATE INDEX IF NOT EXISTS consensus_finality_proofs_sections "
            "ON consensus_finality_proofs(first_section, last_section)";

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

        template <typename T>
        std::expected<T, ConsensusError> load_hashed_payload(DbConnector&     database,
                                                             std::string_view table,
                                                             std::string_view hash) {
            const auto rows = database.select(fmt::format("SELECT payload FROM {} WHERE hash = ?", table),
                                              std::string(table),
                                              { { "hash", std::string(hash) } });
            if (rows.size() != 1) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto payload = rows.front().find("payload");
            if (payload == rows.front().end()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            return decode<T>(payload->second);
        }

        std::expected<FinalityProof, ConsensusError> load_finality_proof(
            DbConnector&                                        database,
            const std::unordered_map<std::string, std::string>& row) {
            const auto finalized_hash   = row.find("finalized_hash");
            const auto child_hash       = row.find("child_hash");
            const auto grandchild_hash  = row.find("grandchild_hash");
            const auto certificate_hash = row.find("decision_certificate_hash");
            if (finalized_hash == row.end() || child_hash == row.end() || grandchild_hash == row.end()
                || certificate_hash == row.end()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            auto finalized =
                load_hashed_payload<Proposal>(database, "consensus_proposals", finalized_hash->second);
            auto child = load_hashed_payload<Proposal>(database, "consensus_proposals", child_hash->second);
            auto grandchild =
                load_hashed_payload<Proposal>(database, "consensus_proposals", grandchild_hash->second);
            auto certificate = load_hashed_payload<QuorumCertificate>(database,
                                                                      "consensus_certificates",
                                                                      certificate_hash->second);
            if (!finalized.has_value() || !child.has_value() || !grandchild.has_value()
                || !certificate.has_value()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            return FinalityProof {
                .finalized_proposal   = std::move(finalized.value()),
                .child_proposal       = std::move(child.value()),
                .grandchild_proposal  = std::move(grandchild.value()),
                .decision_certificate = std::move(certificate.value()),
            };
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
            || !database_->query(std::string(CreateTimeoutVoteTable))
            || !database_->query(std::string(CreateProposalTable))
            || !database_->query(std::string(CreateBatchTable))
            || !database_->query(std::string(CreateCertificateTable))
            || !database_->query(std::string(CreateTimeoutCertificateTable))
            || !database_->query(std::string(CreateFinalityProofTable))
            || !database_->query(std::string(CreateProposalHeightIndex))
            || !database_->query(std::string(CreateBatchHeightIndex))
            || !database_->query(std::string(CreateCertificateHeightIndex))
            || !database_->query(std::string(CreateTimeoutCertificateHeightIndex))
            || !database_->query(std::string(CreateFinalitySectionIndex))) {
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
            const auto votes         = database_->select("SELECT vote_key FROM safety_votes LIMIT 1");
            const auto proposals     = database_->select("SELECT hash FROM consensus_proposals LIMIT 1");
            const auto certificates  = database_->select("SELECT hash FROM consensus_certificates LIMIT 1");
            const auto batches       = database_->select("SELECT hash FROM consensus_batches LIMIT 1");
            const auto timeout_votes = database_->select("SELECT vote_key FROM safety_timeout_votes LIMIT 1");
            const auto timeout_certificates =
                database_->select("SELECT hash FROM consensus_timeout_certificates LIMIT 1");
            const auto finality_proofs = database_->select("SELECT height FROM consensus_finality_proofs LIMIT 1");
            if (!votes.empty() || !timeout_votes.empty() || !proposals.empty() || !batches.empty()
                || !certificates.empty() || !timeout_certificates.empty() || !finality_proofs.empty()) {
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

        const auto key  = vote_key(vote);
        const auto rows = database_->select("SELECT payload FROM safety_votes WHERE vote_key = ?",
                                            "safety_votes",
                                            { { "vote_key", key } });
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

    std::expected<bool, ConsensusError> SafetyStore::persist_timeout_vote(const TimeoutVote& vote,
                                                                          const SafetyState& state) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }

        const auto key  = timeout_vote_key(vote);
        const auto rows = database_->select("SELECT payload FROM safety_timeout_votes WHERE vote_key = ?",
                                            "safety_timeout_votes",
                                            { { "vote_key", key } });
        if (!rows.empty()) {
            const auto payload = rows.front().find("payload");
            if (payload == rows.front().end()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto stored = decode<TimeoutVote>(payload->second);
            if (!stored.has_value()) {
                return std::unexpected(stored.error());
            }
            if (stored.value().highest_certificate_hash != vote.highest_certificate_hash
                || stored.value().signature != vote.signature) {
                return std::unexpected(ConsensusError::ConflictingVote);
            }
            return false;
        }

        if (!database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const bool inserted =
            database_->insert("safety_timeout_votes", { { "vote_key", key }, { "payload", encode(vote) } });
        const auto stored =
            inserted ? persist_state_unlocked(state)
                     : std::expected<void, ConsensusError>(std::unexpected(ConsensusError::StorageFailure));
        if (!stored.has_value() || !database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return true;
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

    std::expected<void, ConsensusError> SafetyStore::persist_batch(const SectionBatchData& batch,
                                                                   std::uint64_t           height) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()
            || !database_->replace("consensus_batches",
                                   { { "hash", batch.header_hash },
                                     { "height", std::to_string(height) },
                                     { "payload", encode(batch) } })) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<void, ConsensusError> SafetyStore::persist_proposal_batch(const Proposal&         proposal,
                                                                            const SectionBatchData& batch) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open() || batch.header_hash != hash_header(proposal.header)
            || !database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }

        const bool proposal_stored = database_->replace("consensus_proposals",
                                                        { { "hash", batch.header_hash },
                                                          { "height", std::to_string(proposal.header.height) },
                                                          { "payload", encode(proposal) } });
        const bool batch_stored    = proposal_stored
                                  && database_->replace("consensus_batches",
                                                        { { "hash", batch.header_hash },
                                                          { "height", std::to_string(proposal.header.height) },
                                                          { "payload", encode(batch) } });
        if (!batch_stored || !database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<bool, ConsensusError> SafetyStore::persist_proposal_batch_vote(const Proposal&         proposal,
                                                                                 const SectionBatchData& batch,
                                                                                 const Vote&             vote,
                                                                                 const SafetyState&      state) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open() || batch.header_hash != hash_header(proposal.header)
            || vote.header_hash != batch.header_hash) {
            return std::unexpected(ConsensusError::StorageFailure);
        }

        const auto key  = vote_key(vote);
        const auto rows = database_->select("SELECT payload FROM safety_votes WHERE vote_key = ?",
                                            "safety_votes",
                                            { { "vote_key", key } });
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
        const bool proposal_stored = database_->replace("consensus_proposals",
                                                        { { "hash", batch.header_hash },
                                                          { "height", std::to_string(proposal.header.height) },
                                                          { "payload", encode(proposal) } });
        const bool batch_stored    = proposal_stored
                                  && database_->replace("consensus_batches",
                                                        { { "hash", batch.header_hash },
                                                          { "height", std::to_string(proposal.header.height) },
                                                          { "payload", encode(batch) } });
        const bool vote_stored =
            batch_stored
            && database_->insert("safety_votes", { { "vote_key", key }, { "payload", encode(vote) } });
        const auto state_stored =
            vote_stored ? persist_state_unlocked(state)
                        : std::expected<void, ConsensusError>(std::unexpected(ConsensusError::StorageFailure));
        if (!state_stored.has_value() || !database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return true;
    }

    std::expected<void, ConsensusError> SafetyStore::persist_certificate_state(
        const QuorumCertificate&            certificate,
        const Proposal&                     proposal,
        const SafetyState&                  state,
        const std::optional<FinalityProof>& proof) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open() || certificate.header_hash != hash_header(proposal.header)
            || !database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const bool proposal_stored    = database_->replace("consensus_proposals",
                                                           { { "hash", certificate.header_hash },
                                                             { "height", std::to_string(proposal.header.height) },
                                                             { "payload", encode(proposal) } });
        const bool certificate_stored = proposal_stored
                                        && database_->replace("consensus_certificates",
                                                              { { "hash", hash_certificate(certificate) },
                                                                { "height", std::to_string(certificate.height) },
                                                                { "payload", encode(certificate) } });
        bool proof_stored = true;
        if (certificate_stored && proof.has_value()) {
            const auto& value = proof.value();
            proof_stored =
                database_
                    ->replace("consensus_finality_proofs",
                              { { "height", std::to_string(value.finalized_proposal.header.height) },
                                { "first_section", std::to_string(value.finalized_proposal.batch.first_section) },
                                { "last_section", std::to_string(value.finalized_proposal.batch.last_section) },
                                { "finalized_hash", hash_header(value.finalized_proposal.header) },
                                { "child_hash", hash_header(value.child_proposal.header) },
                                { "grandchild_hash", hash_header(value.grandchild_proposal.header) },
                                { "decision_certificate_hash", hash_certificate(value.decision_certificate) } });
        }
        const auto state_stored =
            certificate_stored && proof_stored
                ? persist_state_unlocked(state)
                : std::expected<void, ConsensusError>(std::unexpected(ConsensusError::StorageFailure));
        if (!state_stored.has_value() || !database_->query("COMMIT")) {
            database_->query("ROLLBACK");
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

    std::expected<void, ConsensusError> SafetyStore::persist_timeout_certificate_state(
        const TimeoutCertificate& certificate,
        const SafetyState&        state) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open() || !database_->query("BEGIN IMMEDIATE TRANSACTION")) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const bool certificate_stored = database_->replace("consensus_timeout_certificates",
                                                           { { "hash", hash_timeout_certificate(certificate) },
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

    std::expected<std::optional<SectionBatchData>, ConsensusError> SafetyStore::load_batch(
        std::string_view header_hash) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto rows = database_->select("SELECT payload FROM consensus_batches WHERE hash = ?",
                                            "consensus_batches",
                                            { { "hash", std::string(header_hash) } });
        if (rows.empty()) {
            return std::optional<SectionBatchData> {};
        }
        const auto payload = rows.front().find("payload");
        if (payload == rows.front().end()) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const auto value = decode<SectionBatchData>(payload->second);
        if (!value.has_value()) {
            return std::unexpected(value.error());
        }
        return std::optional<SectionBatchData>(value.value());
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

    std::expected<std::vector<TimeoutCertificate>, ConsensusError> SafetyStore::load_timeout_certificates(
        std::uint64_t minimum_height) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        std::vector<TimeoutCertificate> result;
        for (const auto& row :
             database_->select(fmt::format("SELECT payload FROM consensus_timeout_certificates WHERE height >= {}",
                                           minimum_height))) {
            const auto payload = row.find("payload");
            if (payload == row.end()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            const auto value = decode<TimeoutCertificate>(payload->second);
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            result.push_back(value.value());
        }
        return result;
    }

    std::expected<std::vector<FinalityProof>, ConsensusError> SafetyStore::load_finality_proofs_after(
        std::uint64_t height,
        std::size_t   limit) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        std::vector<FinalityProof> result;
        const auto                 rows = database_->select(
            fmt::format("SELECT finalized_hash, child_hash, grandchild_hash, decision_certificate_hash "
                                        "FROM consensus_finality_proofs WHERE height > {} ORDER BY height ASC LIMIT {}",
                        height,
                        limit));
        result.reserve(rows.size());
        for (const auto& row : rows) {
            auto proof = load_finality_proof(*database_, row);
            if (!proof.has_value()) {
                return std::unexpected(proof.error());
            }
            result.push_back(std::move(proof.value()));
        }
        return result;
    }

    std::expected<std::optional<FinalityProof>, ConsensusError> SafetyStore::load_finality_proof_for_section(
        std::uint64_t section) {
        std::lock_guard lock(mutex_);
        if (!database_ || !database_->is_open()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto rows = database_->select(
            fmt::format("SELECT finalized_hash, child_hash, grandchild_hash, decision_certificate_hash "
                        "FROM consensus_finality_proofs WHERE first_section <= {} AND last_section >= {} "
                        "ORDER BY height DESC LIMIT 1",
                        section,
                        section));
        if (rows.empty()) {
            return std::optional<FinalityProof> {};
        }
        auto proof = load_finality_proof(*database_, rows.front());
        if (!proof.has_value()) {
            return std::unexpected(proof.error());
        }
        return std::optional<FinalityProof>(std::move(proof.value()));
    }

    std::string SafetyStore::vote_key(const Vote& vote) {
        return fmt::format("{}:{}:{}:{}:{}",
                           vote.epoch,
                           vote.height,
                           vote.round,
                           std::to_underlying(vote.phase),
                           vote.validator_id);
    }

    std::string SafetyStore::timeout_vote_key(const TimeoutVote& vote) {
        return fmt::format("{}:{}:{}:{}", vote.epoch, vote.height, vote.round, vote.validator_id);
    }

    std::expected<void, ConsensusError> SafetyStore::persist_state_unlocked(const SafetyState& state) {
        if (!database_->replace("safety_state", { { "slot", "active" }, { "payload", encode(state) } })) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return {};
    }

} // namespace ExtraChain::Consensus
