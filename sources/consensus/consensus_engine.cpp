/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/consensus_engine.h"

#include <algorithm>
#include <chrono>
#include <bit>
#include <limits>
#include <tuple>

#include <fmt/format.h>

#include "utils/exc_utils.h"

namespace ExtraChain::Consensus {
    namespace {

        std::string vote_slot(const Vote& vote) {
            return fmt::format("{}:{}:{}:{}:{}",
                               vote.epoch,
                               vote.height,
                               vote.round,
                               std::to_underlying(vote.phase),
                               vote.validator_id);
        }

        std::string timeout_slot(const TimeoutVote& vote) {
            return fmt::format("{}:{}:{}:{}", vote.epoch, vote.height, vote.round, vote.validator_id);
        }

        std::string timeout_round(const TimeoutVote& vote) {
            return fmt::format("{}:{}:{}", vote.epoch, vote.height, vote.round);
        }

        bool bit_is_set(const std::vector<std::uint8_t>& bitmap, std::size_t index) {
            return (bitmap[index / 8] & static_cast<std::uint8_t>(1U << (index % 8))) != 0;
        }

        void set_bit(std::vector<std::uint8_t>& bitmap, std::size_t index) {
            bitmap[index / 8] |= static_cast<std::uint8_t>(1U << (index % 8));
        }

        std::size_t bit_count(const std::vector<std::uint8_t>& bitmap) {
            std::size_t count = 0;
            for (const auto byte : bitmap) {
                count += std::popcount(byte);
            }
            return count;
        }

        bool active_at(const ValidatorRecord& validator, std::uint64_t height) {
            return validator.status == ValidatorStatus::Active && validator.valid_from <= height
                   && (validator.valid_until == 0 || height < validator.valid_until);
        }

        bool extends_batch(const Proposal& parent, const Proposal& child) {
            return parent.batch.last_section != std::numeric_limits<std::uint64_t>::max()
                   && parent.batch.last_section + 1 == child.batch.first_section
                   && parent.header.section_root == child.batch.previous_section_root;
        }
    } // namespace

    ConsensusEngine::ConsensusEngine(ValidatorSetView                 validators,
                                     std::optional<ValidatorIdentity> identity,
                                     std::unique_ptr<SafetyStore>     store,
                                     ProposalValidator                proposal_validator,
                                     std::optional<EpochBootstrapV1>  epoch_bootstrap)
        : validators_(std::move(validators))
        , identity_(std::move(identity))
        , store_(std::move(store))
        , proposal_validator_(std::move(proposal_validator))
        , epoch_bootstrap_(std::move(epoch_bootstrap)) {
    }

    std::expected<void, ConsensusError> ConsensusEngine::initialize() {
        std::lock_guard lock(mutex_);
        if (initialized_) {
            return {};
        }
        if (!store_) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        if (identity_.has_value()) {
            const auto* record = validators_.find(identity_.value().validator_id);
            if (record == nullptr
                || validator_id_for(identity_.value().key.public_key()) != record->validator_id) {
                return std::unexpected(ConsensusError::NotValidator);
            }
        }
        if (epoch_bootstrap_.has_value()
            && !verify_epoch_bootstrap(epoch_bootstrap_.value(), validators_.document())) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        const auto opened = store_->open();
        if (!opened.has_value()) {
            return std::unexpected(opened.error());
        }
        const auto stored = store_->load_state();
        if (!stored.has_value()) {
            return std::unexpected(stored.error());
        }
        if (stored.value().has_value()) {
            safety_state_ = stored.value().value();
            if (safety_state_.protocol_version != ProtocolVersion
                || safety_state_.network_id != validators_.document().network_id
                || safety_state_.epoch != validators_.document().epoch
                || safety_state_.validator_set_hash != validators_.hash()) {
                return std::unexpected(ConsensusError::InvalidValidatorSet);
            }
        } else {
            const auto genesis = genesis_certificate();
            safety_state_      = SafetyState {
                     .protocol_version    = ProtocolVersion,
                     .network_id          = validators_.document().network_id,
                     .epoch               = validators_.document().epoch,
                     .last_voted_height   = genesis.height,
                     .last_voted_round    = 0,
                     .last_voted_phase    = Phase::Genesis,
                     .last_voted_hash     = genesis.header_hash,
                     .highest_certificate = genesis,
                     .locked_certificate  = genesis,
                     .finalized_height =
                    epoch_bootstrap_.has_value() ? epoch_bootstrap_.value().previous_finalized_height : 0,
                     .current_round      = 0,
                     .validator_set_hash = validators_.hash(),
            };
            const auto persisted = store_->persist_state(safety_state_);
            if (!persisted.has_value()) {
                return std::unexpected(persisted.error());
            }
        }
        const auto genesis = genesis_certificate();
        certificates_.insert_or_assign(hash_certificate(genesis), genesis);
        if (safety_state_.highest_certificate.has_value()) {
            const auto& certificate = safety_state_.highest_certificate.value();
            certificates_.insert_or_assign(hash_certificate(certificate), certificate);
            if (certificate.phase != Phase::Genesis) {
                certified_headers_.insert(certificate.header_hash);
            }
        }
        if (safety_state_.locked_certificate.has_value()) {
            const auto& certificate = safety_state_.locked_certificate.value();
            certificates_.insert_or_assign(hash_certificate(certificate), certificate);
            if (certificate.phase != Phase::Genesis) {
                certified_headers_.insert(certificate.header_hash);
            }
        }
        const auto retained_height  = safety_state_.finalized_height > 2 ? safety_state_.finalized_height - 2 : 0;
        const auto stored_proposals = store_->load_proposals(retained_height);
        const auto stored_certificates         = store_->load_certificates(retained_height);
        const auto stored_timeout_certificates = store_->load_timeout_certificates(retained_height);
        if (!stored_proposals.has_value() || !stored_certificates.has_value()
            || !stored_timeout_certificates.has_value()) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        for (const auto& proposal : stored_proposals.value()) {
            if (!verify_proposal(proposal)) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            proposals_.insert_or_assign(hash_header(proposal.header), proposal);
            const auto batch = store_->load_batch(hash_header(proposal.header));
            if (!batch.has_value()) {
                return std::unexpected(batch.error());
            }
            if (batch.value().has_value()) {
                batches_.insert_or_assign(hash_header(proposal.header), batch.value().value());
            }
        }
        for (const auto& certificate : stored_certificates.value()) {
            if (!verify_certificate(certificate)) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            certificates_.insert_or_assign(hash_certificate(certificate), certificate);
            certified_headers_.insert(certificate.header_hash);
        }
        for (const auto& certificate : stored_timeout_certificates.value()) {
            if (!verify_timeout_certificate(certificate)) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            timeout_certificates_.insert_or_assign(hash_timeout_certificate(certificate), certificate);
        }
        for (const auto& certificate : stored_certificates.value()) {
            const auto finalized = finalization_for(certificate);
            const auto proof     = finality_proof_for(certificate);
            if (finalized.has_value() && proof.has_value()) {
                finality_proofs_.insert_or_assign(finalized.value().height, proof.value());
            }
        }
        initialized_ = true;
        return {};
    }

    std::expected<Proposal, ConsensusError> ConsensusEngine::make_proposal(SectionBatchManifest batch,
                                                                           StateCommitmentV2    state,
                                                                           std::uint64_t        round) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !identity_.has_value() || !safety_state_.highest_certificate.has_value()) {
            return std::unexpected(ConsensusError::NotReady);
        }
        const auto& parent = safety_state_.highest_certificate.value();
        const auto  height = parent.height + 1;
        if (!is_local_leader(height, round)) {
            return std::unexpected(ConsensusError::NotLeader);
        }
        if (state.protocol_version != ProtocolVersion || state.network_id != validators_.document().network_id
            || state.epoch != validators_.document().epoch || state.height != height || state.section_root.empty()
            || state.account_state_root.empty() || state.contract_state_root.empty()
            || state.token_registry_root.empty() || state.validator_set_hash != validators_.hash()
            || batch.first_section > batch.last_section || batch.last_section == 0
            || batch.transaction_root.empty() || batch.data_root.empty() || batch.payload_bytes == 0
            || batch.transaction_root != calculate_transaction_root(batch.transaction_hashes)) {
            return std::unexpected(ConsensusError::InvalidRoot);
        }
        if (parent.phase == Phase::Genesis) {
            if (epoch_bootstrap_.has_value()
                && (batch.first_section != epoch_bootstrap_.value().first_dag_section
                    || batch.previous_section_root != epoch_bootstrap_.value().previous_section_root)) {
                return std::unexpected(ConsensusError::InvalidParent);
            }
            if (!epoch_bootstrap_.has_value() && batch.first_section != 0 && batch.previous_section_root.empty()) {
                return std::unexpected(ConsensusError::InvalidParent);
            }
            const auto expected_previous =
                epoch_bootstrap_.has_value() ? epoch_bootstrap_.value().previous_state_commitment : std::string {};
            if (state.previous_state_commitment != expected_previous) {
                return std::unexpected(ConsensusError::InvalidRoot);
            }
        } else {
            const auto parent_proposal = proposals_.find(parent.header_hash);
            if (parent_proposal == proposals_.end()
                || parent_proposal->second.batch.last_section == std::numeric_limits<std::uint64_t>::max()
                || parent_proposal->second.batch.last_section + 1 != batch.first_section
                || parent_proposal->second.header.section_root != batch.previous_section_root
                || state.previous_state_commitment != parent_proposal->second.header.state_commitment) {
                return std::unexpected(ConsensusError::InvalidParent);
            }
        }
        const auto                        batch_root = hash_batch_manifest(batch);
        const auto                        commitment = hash_state_commitment(state);
        std::optional<TimeoutCertificate> timeout_certificate;
        if (round != 0) {
            if (!safety_state_.highest_timeout_certificate.has_value()
                || safety_state_.highest_timeout_certificate.value().height != height
                || safety_state_.highest_timeout_certificate.value().round + 1 != round
                || hash_certificate(safety_state_.highest_timeout_certificate.value().highest_certificate)
                       != hash_certificate(parent)) {
                return std::unexpected(ConsensusError::InvalidRound);
            }
            timeout_certificate = safety_state_.highest_timeout_certificate;
        }
        Proposal proposal {
            .header =
                ConsensusHeader {
                    .protocol_version        = ProtocolVersion,
                    .network_id              = validators_.document().network_id,
                    .epoch                   = validators_.document().epoch,
                    .height                  = height,
                    .round                   = round,
                    .dag_section             = batch.last_section,
                    .parent_certificate_hash = hash_certificate(parent),
                    .section_root            = state.section_root,
                    .transaction_root        = batch.transaction_root,
                    .batch_root              = batch_root,
                    .validator_set_hash      = validators_.hash(),
                    .state_commitment        = commitment,
                    .logical_time            = height,
                },
            .state               = std::move(state),
            .batch               = std::move(batch),
            .parent_certificate  = parent,
            .timeout_certificate = std::move(timeout_certificate),
            .proposer_id         = identity_.value().validator_id,
        };
        const auto signature = sign_payload(identity_.value().key, proposal_signing_payload(proposal));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        proposal.signature = signature.value();
        proposals_.insert_or_assign(hash_header(proposal.header), proposal);
        proposals_created_.fetch_add(1, std::memory_order_relaxed);
        return proposal;
    }

    std::expected<TimeoutVote, ConsensusError> ConsensusEngine::make_timeout_vote(std::uint64_t height,
                                                                                  std::uint64_t round) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !identity_.has_value() || !safety_state_.highest_certificate.has_value()) {
            return std::unexpected(ConsensusError::NotReady);
        }
        if (height != safety_state_.highest_certificate.value().height + 1 || round < safety_state_.current_round
            || std::tie(height, round)
                   < std::tie(safety_state_.last_timeout_height, safety_state_.last_timeout_round)) {
            return std::unexpected(ConsensusError::InvalidRound);
        }

        TimeoutVote vote {
            .protocol_version         = ProtocolVersion,
            .network_id               = validators_.document().network_id,
            .epoch                    = validators_.document().epoch,
            .height                   = height,
            .round                    = round,
            .highest_certificate_hash = hash_certificate(safety_state_.highest_certificate.value()),
            .validator_id             = identity_.value().validator_id,
        };
        const auto signature = sign_payload(identity_.value().key, timeout_vote_signing_payload(vote));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        vote.signature = signature.value();

        auto next_state                = safety_state_;
        next_state.last_timeout_height = height;
        next_state.last_timeout_round  = round;
        const auto persisted           = store_->persist_timeout_vote(vote, next_state);
        if (!persisted.has_value()) {
            return std::unexpected(persisted.error());
        }
        safety_state_ = std::move(next_state);
        timeout_votes_created_.fetch_add(1, std::memory_order_relaxed);
        return vote;
    }

    std::expected<Vote, ConsensusError> ConsensusEngine::accept_proposal(const Proposal& proposal) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !identity_.has_value()) {
            return std::unexpected(ConsensusError::NotValidator);
        }
        if (!verify_proposal(proposal)) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        if (!batches_.contains(hash_header(proposal.header))) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        if (proposal_validator_) {
            const auto validated = proposal_validator_(proposal);
            if (!validated.has_value()) {
                return std::unexpected(validated.error());
            }
        }
        if (!safe_to_vote(proposal)) {
            return std::unexpected(ConsensusError::UnsafeProposal);
        }

        Vote vote {
            .protocol_version = ProtocolVersion,
            .network_id       = proposal.header.network_id,
            .epoch            = proposal.header.epoch,
            .height           = proposal.header.height,
            .round            = proposal.header.round,
            .phase            = Phase::Prepare,
            .header_hash      = hash_header(proposal.header),
            .validator_id     = identity_.value().validator_id,
        };
        const auto signature = sign_payload(identity_.value().key, vote_signing_payload(vote));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        vote.signature = signature.value();

        auto next_state              = safety_state_;
        next_state.last_voted_height = vote.height;
        next_state.last_voted_round  = vote.round;
        next_state.last_voted_phase  = vote.phase;
        next_state.last_voted_hash   = vote.header_hash;
        const auto batch             = batches_.find(vote.header_hash);
        if (batch == batches_.end()) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        const auto persisted = store_->persist_proposal_batch_vote(proposal, batch->second, vote, next_state);
        if (!persisted.has_value()) {
            return std::unexpected(persisted.error());
        }
        safety_state_ = std::move(next_state);
        proposals_.insert_or_assign(vote.header_hash, proposal);
        votes_created_.fetch_add(1, std::memory_order_relaxed);
        return vote;
    }

    std::expected<void, ConsensusError> ConsensusEngine::observe_proposal(const Proposal& proposal) {
        std::lock_guard lock(mutex_);
        if (!initialized_) {
            return std::unexpected(ConsensusError::NotReady);
        }
        if (!verify_proposal(proposal)) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        if (proposal_validator_) {
            const auto validated = proposal_validator_(proposal);
            if (!validated.has_value() && validated.error() != ConsensusError::DataUnavailable) {
                return std::unexpected(validated.error());
            }
        }
        proposals_.insert_or_assign(hash_header(proposal.header), proposal);
        return {};
    }

    std::expected<void, ConsensusError> ConsensusEngine::stage_batch(SectionBatchData batch) {
        std::lock_guard lock(mutex_);
        const auto      started = std::chrono::steady_clock::now();
        if (!initialized_) {
            return std::unexpected(ConsensusError::NotReady);
        }
        const auto proposal = proposals_.find(batch.header_hash);
        if (proposal == proposals_.end()) {
            return std::unexpected(ConsensusError::InvalidParent);
        }
        if (hash_batch_manifest(batch.manifest) != proposal->second.header.batch_root
            || hash_batch_manifest(batch.manifest) != hash_batch_manifest(proposal->second.batch)
            || batch.manifest.first_section > batch.manifest.last_section || batch.sections.empty()
            || batch.sections.size() != batch.manifest.last_section - batch.manifest.first_section + 1
            || batch.manifest.transaction_root != calculate_transaction_root(batch.manifest.transaction_hashes)) {
            return std::unexpected(ConsensusError::InvalidRoot);
        }
        std::uint64_t payload_bytes = 0;
        std::uint64_t expected      = batch.manifest.first_section;
        for (const auto& [section, bytes] : batch.sections) {
            if (section != expected || bytes.size() > std::numeric_limits<std::uint64_t>::max() - payload_bytes) {
                return std::unexpected(ConsensusError::InvalidRoot);
            }
            payload_bytes += bytes.size();
            ++expected;
        }
        if (payload_bytes != batch.manifest.payload_bytes
            || calculate_data_root(batch.sections) != batch.manifest.data_root) {
            return std::unexpected(ConsensusError::InvalidRoot);
        }
        if (!identity_.has_value()) {
            const auto stored = store_->persist_proposal_batch(proposal->second, batch);
            if (!stored.has_value()) {
                return std::unexpected(stored.error());
            }
        }
        batches_.insert_or_assign(batch.header_hash, std::move(batch));
        batches_staged_.fetch_add(1, std::memory_order_relaxed);
        stage_nanoseconds_.fetch_add(static_cast<std::uint64_t>(
                                         std::chrono::duration_cast<std::chrono::nanoseconds>(
                                             std::chrono::steady_clock::now() - started)
                                             .count()),
                                     std::memory_order_relaxed);
        return {};
    }

    std::expected<VoteAcceptance, ConsensusError> ConsensusEngine::accept_vote(const Vote& vote) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !verify_vote(vote)) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        VoteAcceptance result;
        const auto     slot  = vote_slot(vote);
        const auto     prior = observed_vote_slots_.find(slot);
        if (prior != observed_vote_slots_.end() && prior->second.header_hash != vote.header_hash) {
            result.equivocation = EquivocationProof { .first = prior->second, .second = vote };
            return result;
        }
        observed_vote_slots_.insert_or_assign(slot, vote);

        const auto proposal = proposals_.find(vote.header_hash);
        if (proposal == proposals_.end()) {
            return std::unexpected(ConsensusError::InvalidParent);
        }
        if (certified_headers_.contains(vote.header_hash)) {
            return result;
        }
        votes_[vote.header_hash].insert_or_assign(vote.validator_id, vote);
        if (!identity_.has_value() || proposal->second.proposer_id != identity_.value().validator_id
            || votes_[vote.header_hash].size() < validators_.quorum()) {
            return result;
        }

        QuorumCertificate certificate {
            .protocol_version = ProtocolVersion,
            .network_id       = vote.network_id,
            .epoch            = vote.epoch,
            .height           = vote.height,
            .round            = vote.round,
            .phase            = Phase::Prepare,
            .header_hash      = vote.header_hash,
            .signer_bitmap    = std::vector<std::uint8_t>((validators_.active().size() + 7) / 8, 0),
        };
        for (std::size_t index = 0; index < validators_.active().size(); ++index) {
            const auto found = votes_[vote.header_hash].find(validators_.active()[index].validator_id);
            if (found == votes_[vote.header_hash].end()) {
                continue;
            }
            set_bit(certificate.signer_bitmap, index);
            certificate.signatures.push_back(found->second.signature);
        }
        if (!verify_certificate(certificate)) {
            return std::unexpected(ConsensusError::InvalidCertificate);
        }
        result.certificate = std::move(certificate);
        return result;
    }

    std::expected<TimeoutAcceptance, ConsensusError> ConsensusEngine::accept_timeout_vote(
        const TimeoutVote& vote) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !verify_timeout_vote(vote)) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        TimeoutAcceptance result;
        const auto        slot  = timeout_slot(vote);
        const auto        prior = observed_timeout_slots_.find(slot);
        if (prior != observed_timeout_slots_.end()
            && prior->second.highest_certificate_hash != vote.highest_certificate_hash) {
            result.equivocation = std::pair { prior->second, vote };
            return result;
        }
        observed_timeout_slots_.insert_or_assign(slot, vote);

        const auto certificate = certificates_.find(vote.highest_certificate_hash);
        if (certificate == certificates_.end()) {
            return std::unexpected(ConsensusError::InvalidParent);
        }
        const auto round_key = timeout_round(vote) + ':' + vote.highest_certificate_hash;
        auto&      votes     = timeout_votes_[round_key];
        votes.insert_or_assign(vote.validator_id, vote);
        if (votes.size() < validators_.quorum()) {
            return result;
        }

        TimeoutCertificate timeout_certificate {
            .protocol_version    = ProtocolVersion,
            .network_id          = vote.network_id,
            .epoch               = vote.epoch,
            .height              = vote.height,
            .round               = vote.round,
            .highest_certificate = certificate->second,
            .signer_bitmap       = std::vector<std::uint8_t>((validators_.active().size() + 7) / 8, 0),
        };
        for (std::size_t index = 0; index < validators_.active().size(); ++index) {
            const auto found = votes.find(validators_.active()[index].validator_id);
            if (found == votes.end()) {
                continue;
            }
            set_bit(timeout_certificate.signer_bitmap, index);
            timeout_certificate.signatures.push_back(found->second.signature);
        }
        if (!verify_timeout_certificate(timeout_certificate)) {
            return std::unexpected(ConsensusError::InvalidCertificate);
        }
        result.certificate = std::move(timeout_certificate);
        return result;
    }

    std::expected<void, ConsensusError> ConsensusEngine::accept_timeout_certificate(
        const TimeoutCertificate& certificate) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !verify_timeout_certificate(certificate)) {
            return std::unexpected(ConsensusError::InvalidCertificate);
        }
        auto next_state = safety_state_;
        if (!next_state.highest_certificate.has_value()
            || newer(certificate.highest_certificate, next_state.highest_certificate.value())) {
            next_state.highest_certificate = certificate.highest_certificate;
        }
        if (certificate.height != next_state.highest_certificate.value().height + 1) {
            return std::unexpected(ConsensusError::InvalidHeight);
        }
        if (certificate.height == next_state.highest_certificate.value().height + 1
            && certificate.round + 1 > next_state.current_round) {
            next_state.current_round = certificate.round + 1;
        }
        if (!next_state.highest_timeout_certificate.has_value()
            || std::tie(certificate.height, certificate.round)
                   > std::tie(next_state.highest_timeout_certificate.value().height,
                              next_state.highest_timeout_certificate.value().round)) {
            next_state.highest_timeout_certificate = certificate;
        }
        next_state.last_timeout_certificate_hash = hash_timeout_certificate(certificate);
        const auto stored = store_->persist_timeout_certificate_state(certificate, next_state);
        if (!stored.has_value()) {
            return std::unexpected(stored.error());
        }
        timeout_certificates_.insert_or_assign(hash_timeout_certificate(certificate), certificate);
        certificates_.insert_or_assign(hash_certificate(certificate.highest_certificate),
                                       certificate.highest_certificate);
        if (certificate.highest_certificate.phase != Phase::Genesis) {
            certified_headers_.insert(certificate.highest_certificate.header_hash);
        }
        safety_state_ = std::move(next_state);
        return {};
    }

    std::expected<std::optional<FinalizedCheckpoint>, ConsensusError> ConsensusEngine::accept_certificate(
        const QuorumCertificate& certificate) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !verify_certificate(certificate)) {
            return std::unexpected(ConsensusError::InvalidCertificate);
        }
        const auto proposal = proposals_.find(certificate.header_hash);
        if (certificate.phase != Phase::Genesis && proposal == proposals_.end()) {
            return std::unexpected(ConsensusError::InvalidParent);
        }
        auto next_state = safety_state_;
        if (!next_state.highest_certificate.has_value()
            || newer(certificate, next_state.highest_certificate.value())) {
            next_state.highest_certificate = certificate;
            next_state.current_round       = 0;
            next_state.highest_timeout_certificate.reset();
        }
        if (proposal != proposals_.end() && proposal->second.parent_certificate.phase != Phase::Genesis
            && (!next_state.locked_certificate.has_value()
                || newer(proposal->second.parent_certificate, next_state.locked_certificate.value()))) {
            next_state.locked_certificate = proposal->second.parent_certificate;
        }

        auto finalized = finalization_for(certificate);
        if (finalized.has_value() && finalized.value().height > next_state.finalized_height) {
            next_state.finalized_height = finalized.value().height;
        } else {
            finalized.reset();
        }
        std::optional<FinalityProof> finality_proof;
        if (finalized.has_value()) {
            finality_proof = finality_proof_for(certificate);
            if (!finality_proof.has_value()) {
                return std::unexpected(ConsensusError::InvalidParent);
            }
        }
        const auto stored = store_->persist_certificate_state(certificate, next_state, finality_proof);
        if (!stored.has_value()) {
            return std::unexpected(stored.error());
        }
        certificates_.insert_or_assign(hash_certificate(certificate), certificate);
        certified_headers_.insert(certificate.header_hash);
        votes_.erase(certificate.header_hash);
        safety_state_ = std::move(next_state);
        certificates_accepted_.fetch_add(1, std::memory_order_relaxed);
        if (finalized.has_value()) {
            finality_proofs_.insert_or_assign(finalized.value().height, finality_proof.value());
            checkpoints_finalized_.fetch_add(1, std::memory_order_relaxed);
        }
        prune_memory(safety_state_.finalized_height);
        return finalized;
    }

    bool ConsensusEngine::verify_certificate(const QuorumCertificate& certificate) const {
        std::lock_guard lock(mutex_);
        if (certificate.protocol_version != ProtocolVersion
            || certificate.network_id != validators_.document().network_id
            || certificate.epoch != validators_.document().epoch || certificate.header_hash.empty()) {
            return false;
        }
        if (certificate.phase == Phase::Genesis) {
            const auto expected = genesis_certificate();
            return certificate.height == expected.height && certificate.round == 0
                   && certificate.signatures.empty()
                   && certificate.signer_bitmap
                          == std::vector<std::uint8_t>((validators_.active().size() + 7) / 8, 0)
                   && certificate.header_hash == expected.header_hash;
        }
        if (certificate.phase != Phase::Prepare
            || certificate.signer_bitmap.size() != (validators_.active().size() + 7) / 8
            || bit_count(certificate.signer_bitmap) != certificate.signatures.size()
            || certificate.signatures.size() < validators_.quorum()) {
            return false;
        }
        if (validators_.active().size() % 8 != 0) {
            const auto valid_bits = static_cast<std::uint8_t>((1U << (validators_.active().size() % 8)) - 1U);
            if ((certificate.signer_bitmap.back() & static_cast<std::uint8_t>(~valid_bits)) != 0) {
                return false;
            }
        }

        std::size_t signature_index = 0;
        for (std::size_t index = 0; index < validators_.active().size(); ++index) {
            if (!bit_is_set(certificate.signer_bitmap, index)) {
                continue;
            }
            const auto& validator = validators_.active()[index];
            Vote        vote {
                       .protocol_version = certificate.protocol_version,
                       .network_id       = certificate.network_id,
                       .epoch            = certificate.epoch,
                       .height           = certificate.height,
                       .round            = certificate.round,
                       .phase            = certificate.phase,
                       .header_hash      = certificate.header_hash,
                       .validator_id     = validator.validator_id,
                       .signature        = certificate.signatures[signature_index++],
            };
            if (!active_at(validator, vote.height) || !verify_vote(vote)) {
                return false;
            }
        }
        return true;
    }

    bool ConsensusEngine::verify_timeout_certificate(const TimeoutCertificate& certificate) const {
        std::lock_guard lock(mutex_);
        if (certificate.protocol_version != ProtocolVersion
            || certificate.network_id != validators_.document().network_id
            || certificate.epoch != validators_.document().epoch || certificate.height == 0
            || certificate.height != certificate.highest_certificate.height + 1
            || !verify_certificate(certificate.highest_certificate)
            || certificate.signer_bitmap.size() != (validators_.active().size() + 7) / 8
            || bit_count(certificate.signer_bitmap) != certificate.signatures.size()
            || certificate.signatures.size() < validators_.quorum()) {
            return false;
        }
        if (validators_.active().size() % 8 != 0) {
            const auto valid_bits = static_cast<std::uint8_t>((1U << (validators_.active().size() % 8)) - 1U);
            if ((certificate.signer_bitmap.back() & static_cast<std::uint8_t>(~valid_bits)) != 0) {
                return false;
            }
        }

        std::size_t signature_index = 0;
        for (std::size_t index = 0; index < validators_.active().size(); ++index) {
            if (!bit_is_set(certificate.signer_bitmap, index)) {
                continue;
            }
            TimeoutVote vote {
                .protocol_version         = certificate.protocol_version,
                .network_id               = certificate.network_id,
                .epoch                    = certificate.epoch,
                .height                   = certificate.height,
                .round                    = certificate.round,
                .highest_certificate_hash = hash_certificate(certificate.highest_certificate),
                .validator_id             = validators_.active()[index].validator_id,
                .signature                = certificate.signatures[signature_index++],
            };
            if (!active_at(validators_.active()[index], vote.height) || !verify_timeout_vote(vote)) {
                return false;
            }
        }
        return true;
    }

    bool ConsensusEngine::verify_finality_proof(const FinalityProof& proof) const {
        std::lock_guard lock(mutex_);
        const auto&     first  = proof.finalized_proposal;
        const auto&     second = proof.child_proposal;
        const auto&     third  = proof.grandchild_proposal;
        return verify_proposal(first) && verify_proposal(second) && verify_proposal(third)
               && first.header.height + 1 == second.header.height
               && second.header.height + 1 == third.header.height
               && second.parent_certificate.height == first.header.height
               && third.parent_certificate.height == second.header.height
               && proof.decision_certificate.height == third.header.height
               && second.parent_certificate.header_hash == hash_header(first.header)
               && third.parent_certificate.header_hash == hash_header(second.header)
               && proof.decision_certificate.header_hash == hash_header(third.header)
               && second.state.previous_state_commitment == first.header.state_commitment
               && third.state.previous_state_commitment == second.header.state_commitment
               && extends_batch(first, second) && extends_batch(second, third)
               && verify_certificate(second.parent_certificate) && verify_certificate(third.parent_certificate)
               && verify_certificate(proof.decision_certificate);
    }

    std::expected<std::vector<FinalityProof>, ConsensusError> ConsensusEngine::finality_proofs_after(
        std::uint64_t height,
        std::size_t   limit) const {
        std::lock_guard lock(mutex_);
        const auto      stored = store_->load_finality_proofs_after(height, limit);
        if (!stored.has_value()) {
            return std::unexpected(stored.error());
        }
        for (const auto& proof : stored.value()) {
            if (!verify_finality_proof(proof)) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
        }
        return stored.value();
    }

    std::expected<std::optional<FinalityProof>, ConsensusError> ConsensusEngine::finality_proof_for_section(
        std::uint64_t section) const {
        std::lock_guard lock(mutex_);
        for (const auto& [_, proof] : finality_proofs_) {
            if (proof.finalized_proposal.batch.first_section <= section
                && section <= proof.finalized_proposal.batch.last_section) {
                return std::optional<FinalityProof>(proof);
            }
        }
        const auto stored = store_->load_finality_proof_for_section(section);
        if (!stored.has_value()) {
            return std::unexpected(stored.error());
        }
        if (stored.value().has_value() && !verify_finality_proof(stored.value().value())) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return stored.value();
    }

    std::expected<std::optional<TransactionInclusionProofV1>, ConsensusError> ConsensusEngine::
        transaction_inclusion_proof(std::string_view transaction_hash) const {
        std::lock_guard lock(mutex_);
        if (transaction_hash.empty()) {
            return std::unexpected(ConsensusError::InvalidProof);
        }
        std::uint64_t cursor = 0;
        while (true) {
            const auto stored = store_->load_finality_proofs_after(cursor, MaximumShadowSyncProofs);
            if (!stored.has_value()) {
                return std::unexpected(stored.error());
            }
            for (const auto& finality_proof : stored.value()) {
                if (!verify_finality_proof(finality_proof)) {
                    return std::unexpected(ConsensusError::StorageFailure);
                }
                const auto& hashes = finality_proof.finalized_proposal.batch.transaction_hashes;
                const auto  found  = std::ranges::find(hashes, transaction_hash);
                if (found != hashes.end()) {
                    const auto index = static_cast<std::size_t>(std::distance(hashes.begin(), found));
                    const auto proof = make_merkle_proof(hashes, index);
                    if (!proof.has_value()) {
                        return std::unexpected(proof.error());
                    }
                    return TransactionInclusionProofV1 {
                        .protocol_version = ProtocolVersion,
                        .network_id       = validators_.document().network_id,
                        .epoch            = validators_.document().epoch,
                        .height           = finality_proof.finalized_proposal.header.height,
                        .transaction_hash = std::string(transaction_hash),
                        .merkle_proof     = proof.value(),
                        .finality_proof   = finality_proof,
                    };
                }
                cursor = finality_proof.finalized_proposal.header.height;
            }
            if (stored.value().size() < MaximumShadowSyncProofs) {
                break;
            }
        }
        return std::optional<TransactionInclusionProofV1> {};
    }

    bool ConsensusEngine::verify_transaction_inclusion_proof(const TransactionInclusionProofV1& proof) const {
        std::lock_guard lock(mutex_);
        const auto&     proposal = proof.finality_proof.finalized_proposal;
        if (proof.protocol_version != ProtocolVersion || proof.network_id != validators_.document().network_id
            || proof.epoch != validators_.document().epoch || proof.height != proposal.header.height
            || proof.transaction_hash.empty()) {
            return false;
        }
        return verify_finality_proof(proof.finality_proof)
               && proposal.header.transaction_root == proposal.batch.transaction_root
               && verify_merkle_proof(proof.transaction_hash, proof.merkle_proof, proposal.batch.transaction_root);
    }

    QuorumCertificate ConsensusEngine::genesis_certificate() const {
        return epoch_bootstrap_.has_value() ? make_epoch_genesis_certificate(validators_, epoch_bootstrap_.value())
                                            : make_genesis_certificate(validators_);
    }

    const ValidatorSetView& ConsensusEngine::validators() const noexcept {
        return validators_;
    }

    const SafetyState& ConsensusEngine::safety_state() const noexcept {
        return safety_state_;
    }

    const std::optional<ValidatorIdentity>& ConsensusEngine::identity() const noexcept {
        return identity_;
    }

    const std::optional<EpochBootstrapV1>& ConsensusEngine::epoch_bootstrap() const noexcept {
        return epoch_bootstrap_;
    }

    bool ConsensusEngine::is_local_leader(std::uint64_t height, std::uint64_t round) const {
        return identity_.has_value()
               && validators_.leader(height, round).validator_id == identity_.value().validator_id;
    }

    std::optional<Proposal> ConsensusEngine::proposal_for(std::string_view header_hash) const {
        std::lock_guard lock(mutex_);
        const auto      proposal = proposals_.find(std::string(header_hash));
        return proposal == proposals_.end() ? std::nullopt : std::optional<Proposal>(proposal->second);
    }

    std::optional<SectionBatchData> ConsensusEngine::batch_for(std::string_view header_hash) const {
        std::lock_guard lock(mutex_);
        const auto      batch = batches_.find(std::string(header_hash));
        if (batch != batches_.end()) {
            return batch->second;
        }
        const auto stored = store_->load_batch(header_hash);
        return stored.has_value() ? stored.value() : std::nullopt;
    }

    ConsensusMetricsSnapshot ConsensusEngine::metrics() const noexcept {
        return ConsensusMetricsSnapshot {
            .proposals_created = proposals_created_.load(std::memory_order_relaxed),
            .batches_staged    = batches_staged_.load(std::memory_order_relaxed),
            .votes_created     = votes_created_.load(std::memory_order_relaxed),
            .timeout_votes     = timeout_votes_created_.load(std::memory_order_relaxed),
            .certificates      = certificates_accepted_.load(std::memory_order_relaxed),
            .finalized         = checkpoints_finalized_.load(std::memory_order_relaxed),
            .stage_nanoseconds = stage_nanoseconds_.load(std::memory_order_relaxed),
        };
    }

    bool ConsensusEngine::verify_proposal(const Proposal& proposal) const {
        if (proposal.header.protocol_version != ProtocolVersion
            || proposal.header.network_id != validators_.document().network_id
            || proposal.header.epoch != validators_.document().epoch || proposal.header.height == 0
            || proposal.header.validator_set_hash != validators_.hash()
            || proposal.header.parent_certificate_hash != hash_certificate(proposal.parent_certificate)
            || proposal.header.height != proposal.parent_certificate.height + 1
            || proposal.header.section_root.empty() || proposal.header.transaction_root.empty()
            || proposal.batch.first_section > proposal.batch.last_section
            || proposal.batch.last_section != proposal.header.dag_section
            || proposal.batch.transaction_root != proposal.header.transaction_root
            || proposal.batch.transaction_root != calculate_transaction_root(proposal.batch.transaction_hashes)
            || proposal.header.batch_root != hash_batch_manifest(proposal.batch)
            || proposal.batch.data_root.empty() || proposal.batch.payload_bytes == 0
            || proposal.state.protocol_version != ProtocolVersion
            || proposal.state.network_id != proposal.header.network_id
            || proposal.state.epoch != proposal.header.epoch || proposal.state.height != proposal.header.height
            || proposal.state.section_root != proposal.header.section_root
            || proposal.state.validator_set_hash != proposal.header.validator_set_hash
            || proposal.state.account_state_root.empty() || proposal.state.contract_state_root.empty()
            || proposal.state.token_registry_root.empty()
            || proposal.header.state_commitment != hash_state_commitment(proposal.state)
            || !verify_certificate(proposal.parent_certificate)) {
            return false;
        }
        if ((proposal.header.round == 0 && proposal.timeout_certificate.has_value())
            || (proposal.header.round != 0
                && (!proposal.timeout_certificate.has_value()
                    || !verify_timeout_certificate(proposal.timeout_certificate.value())
                    || proposal.timeout_certificate.value().height != proposal.header.height
                    || proposal.timeout_certificate.value().round + 1 != proposal.header.round
                    || hash_certificate(proposal.timeout_certificate.value().highest_certificate)
                           != hash_certificate(proposal.parent_certificate)))) {
            return false;
        }
        if (proposal.parent_certificate.phase == Phase::Genesis) {
            if (epoch_bootstrap_.has_value()
                && (proposal.batch.first_section != epoch_bootstrap_.value().first_dag_section
                    || proposal.batch.previous_section_root != epoch_bootstrap_.value().previous_section_root)) {
                return false;
            }
            if (!epoch_bootstrap_.has_value() && proposal.batch.first_section == 0
                && !proposal.batch.previous_section_root.empty()) {
                return false;
            }
            const auto expected_previous =
                epoch_bootstrap_.has_value() ? epoch_bootstrap_.value().previous_state_commitment : std::string {};
            if (proposal.state.previous_state_commitment != expected_previous) {
                return false;
            }
        } else {
            const auto parent = proposals_.find(proposal.parent_certificate.header_hash);
            if (parent != proposals_.end()
                && (parent->second.batch.last_section == std::numeric_limits<std::uint64_t>::max()
                    || parent->second.batch.last_section + 1 != proposal.batch.first_section
                    || parent->second.header.section_root != proposal.batch.previous_section_root)) {
                return false;
            }
            if (parent != proposals_.end()
                && proposal.state.previous_state_commitment != parent->second.header.state_commitment) {
                return false;
            }
        }
        const auto* proposer = validators_.find(proposal.proposer_id);
        return proposer != nullptr && active_at(*proposer, proposal.header.height)
               && validators_.leader(proposal.header.height, proposal.header.round).validator_id
                      == proposal.proposer_id
               && verify_payload(proposer->consensus_public_key,
                                 proposal_signing_payload(proposal),
                                 proposal.signature);
    }

    bool ConsensusEngine::verify_vote(const Vote& vote) const {
        if (vote.protocol_version != ProtocolVersion || vote.network_id != validators_.document().network_id
            || vote.epoch != validators_.document().epoch || vote.height == 0 || vote.phase != Phase::Prepare
            || vote.header_hash.empty()) {
            return false;
        }
        const auto* validator = validators_.find(vote.validator_id);
        return validator != nullptr && active_at(*validator, vote.height)
               && verify_payload(validator->consensus_public_key, vote_signing_payload(vote), vote.signature);
    }

    bool ConsensusEngine::verify_timeout_vote(const TimeoutVote& vote) const {
        if (vote.protocol_version != ProtocolVersion || vote.network_id != validators_.document().network_id
            || vote.epoch != validators_.document().epoch || vote.height == 0
            || vote.highest_certificate_hash.empty()) {
            return false;
        }
        const auto* validator = validators_.find(vote.validator_id);
        return validator != nullptr && active_at(*validator, vote.height)
               && verify_payload(validator->consensus_public_key,
                                 timeout_vote_signing_payload(vote),
                                 vote.signature);
    }

    bool ConsensusEngine::safe_to_vote(const Proposal& proposal) const {
        if (proposal.header.round < safety_state_.current_round) {
            return false;
        }
        if (safety_state_.last_voted_height > proposal.header.height
            || (safety_state_.last_voted_height == proposal.header.height
                && safety_state_.last_voted_round > proposal.header.round)) {
            return false;
        }
        if (safety_state_.last_voted_height == proposal.header.height
            && safety_state_.last_voted_round == proposal.header.round
            && safety_state_.last_voted_phase == Phase::Prepare && !safety_state_.last_voted_hash.empty()
            && safety_state_.last_voted_hash != hash_header(proposal.header)) {
            return false;
        }
        if (!safety_state_.locked_certificate.has_value()) {
            return true;
        }
        const auto& locked = safety_state_.locked_certificate.value();
        return proposal.parent_certificate.header_hash == locked.header_hash
               || newer(proposal.parent_certificate, locked);
    }

    bool ConsensusEngine::newer(const QuorumCertificate& left, const QuorumCertificate& right) noexcept {
        return std::tie(left.height, left.round) > std::tie(right.height, right.round);
    }

    std::optional<FinalizedCheckpoint> ConsensusEngine::finalization_for(
        const QuorumCertificate& certificate) const {
        const auto child = proposals_.find(certificate.header_hash);
        if (child == proposals_.end()) {
            return std::nullopt;
        }
        const auto parent = proposals_.find(child->second.parent_certificate.header_hash);
        if (parent == proposals_.end()) {
            return std::nullopt;
        }
        const auto grandparent = proposals_.find(parent->second.parent_certificate.header_hash);
        if (grandparent == proposals_.end()
            || grandparent->second.header.height + 1 != parent->second.header.height
            || parent->second.header.height + 1 != child->second.header.height) {
            return std::nullopt;
        }
        return FinalizedCheckpoint {
            .height            = grandparent->second.header.height,
            .dag_section       = grandparent->second.header.dag_section,
            .first_dag_section = grandparent->second.batch.first_section,
            .header_hash       = hash_header(grandparent->second.header),
            .section_root      = grandparent->second.header.section_root,
            .transaction_root  = grandparent->second.header.transaction_root,
            .batch_root        = grandparent->second.header.batch_root,
            .certificate_hash  = hash_certificate(certificate),
        };
    }

    std::optional<FinalityProof> ConsensusEngine::finality_proof_for(const QuorumCertificate& certificate) const {
        const auto child = proposals_.find(certificate.header_hash);
        if (child == proposals_.end()) {
            return std::nullopt;
        }
        const auto parent = proposals_.find(child->second.parent_certificate.header_hash);
        if (parent == proposals_.end()) {
            return std::nullopt;
        }
        const auto grandparent = proposals_.find(parent->second.parent_certificate.header_hash);
        if (grandparent == proposals_.end()) {
            return std::nullopt;
        }
        return FinalityProof {
            .finalized_proposal   = grandparent->second,
            .child_proposal       = parent->second,
            .grandchild_proposal  = child->second,
            .decision_certificate = certificate,
        };
    }

    void ConsensusEngine::prune_memory(std::uint64_t finalized_height) {
        if (finalized_height <= 2) {
            return;
        }
        const auto minimum_height = finalized_height - 2;
        std::erase_if(proposals_, [minimum_height](const auto& item) {
            return item.second.header.height < minimum_height;
        });
        std::erase_if(batches_, [minimum_height, this](const auto& item) {
            const auto proposal = proposals_.find(item.first);
            return proposal == proposals_.end() || proposal->second.header.height < minimum_height;
        });
        std::erase_if(certificates_, [minimum_height](const auto& item) {
            return item.second.phase != Phase::Genesis && item.second.height < minimum_height;
        });
        certified_headers_.clear();
        for (const auto& [_, certificate] : certificates_) {
            if (certificate.phase != Phase::Genesis) {
                certified_headers_.insert(certificate.header_hash);
            }
        }
        std::erase_if(votes_, [minimum_height, this](const auto& item) {
            const auto proposal = proposals_.find(item.first);
            return proposal == proposals_.end() || proposal->second.header.height < minimum_height;
        });
        std::erase_if(observed_vote_slots_, [minimum_height](const auto& item) {
            return item.second.height < minimum_height;
        });
        std::erase_if(timeout_votes_, [minimum_height](const auto& item) {
            return item.second.empty() || item.second.begin()->second.height < minimum_height;
        });
        std::erase_if(observed_timeout_slots_, [minimum_height](const auto& item) {
            return item.second.height < minimum_height;
        });
        std::erase_if(timeout_certificates_, [minimum_height](const auto& item) {
            return item.second.height < minimum_height;
        });
        std::erase_if(finality_proofs_, [minimum_height](const auto& item) {
            return item.first < minimum_height;
        });
    }

} // namespace ExtraChain::Consensus
