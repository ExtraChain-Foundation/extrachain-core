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
#include <bit>
#include <tuple>

#include <fmt/format.h>

#include "utils/exc_utils.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view GenesisDomain = "EXC_CONSENSUS_GENESIS_V1";
        constexpr std::string_view StateDomain   = "EXC_CONSENSUS_STATE_V1";

        std::string vote_slot(const Vote& vote) {
            return fmt::format("{}:{}:{}:{}:{}",
                               vote.epoch,
                               vote.height,
                               vote.round,
                               std::to_underlying(vote.phase),
                               vote.validator_id);
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
    } // namespace

    ConsensusEngine::ConsensusEngine(ValidatorSetView                 validators,
                                     std::optional<ValidatorIdentity> identity,
                                     std::unique_ptr<SafetyStore>     store,
                                     ProposalValidator                proposal_validator)
        : validators_(std::move(validators))
        , identity_(std::move(identity))
        , store_(std::move(store))
        , proposal_validator_(std::move(proposal_validator)) {
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
                     .last_voted_height   = 0,
                     .last_voted_round    = 0,
                     .last_voted_phase    = Phase::Genesis,
                     .last_voted_hash     = genesis.header_hash,
                     .highest_certificate = genesis,
                     .locked_certificate  = genesis,
                     .finalized_height    = 0,
                     .validator_set_hash  = validators_.hash(),
            };
            const auto persisted = store_->persist_state(safety_state_);
            if (!persisted.has_value()) {
                return std::unexpected(persisted.error());
            }
        }
        const auto genesis = genesis_certificate();
        certificates_.insert_or_assign(hash_certificate(genesis), genesis);
        const auto retained_height  = safety_state_.finalized_height > 2 ? safety_state_.finalized_height - 2 : 0;
        const auto stored_proposals = store_->load_proposals(retained_height);
        const auto stored_certificates = store_->load_certificates(retained_height);
        if (!stored_proposals.has_value() || !stored_certificates.has_value()) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        for (const auto& proposal : stored_proposals.value()) {
            if (!verify_proposal(proposal)) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            proposals_.insert_or_assign(hash_header(proposal.header), proposal);
        }
        for (const auto& certificate : stored_certificates.value()) {
            if (!verify_certificate(certificate)) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            certificates_.insert_or_assign(hash_certificate(certificate), certificate);
            certified_headers_.insert(certificate.header_hash);
        }
        initialized_ = true;
        return {};
    }

    std::expected<Proposal, ConsensusError> ConsensusEngine::make_proposal(std::uint64_t dag_section,
                                                                           std::string   section_root,
                                                                           std::string   transaction_root,
                                                                           std::uint64_t round) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !identity_.has_value() || !safety_state_.highest_certificate.has_value()) {
            return std::unexpected(ConsensusError::NotReady);
        }
        const auto& parent = safety_state_.highest_certificate.value();
        const auto  height = parent.height + 1;
        if (!is_local_leader(height, round)) {
            return std::unexpected(ConsensusError::NotLeader);
        }
        if (section_root.empty() || transaction_root.empty() || dag_section == 0) {
            return std::unexpected(ConsensusError::InvalidRoot);
        }
        const auto commitment = state_commitment(parent, section_root, transaction_root);
        Proposal   proposal {
              .header =
                ConsensusHeader {
                      .protocol_version        = ProtocolVersion,
                      .network_id              = validators_.document().network_id,
                      .epoch                   = validators_.document().epoch,
                      .height                  = height,
                      .round                   = round,
                      .dag_section             = dag_section,
                      .parent_certificate_hash = hash_certificate(parent),
                      .section_root            = std::move(section_root),
                      .transaction_root        = std::move(transaction_root),
                      .validator_set_hash      = validators_.hash(),
                      .state_commitment        = commitment,
                      .logical_time            = height,
                },
              .parent_certificate = parent,
              .proposer_id        = identity_.value().validator_id,
        };
        const auto signature = sign_payload(identity_.value().key, proposal_signing_payload(proposal));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        proposal.signature   = signature.value();
        const auto persisted = store_->persist_proposal(proposal);
        if (!persisted.has_value()) {
            return std::unexpected(persisted.error());
        }
        proposals_.insert_or_assign(hash_header(proposal.header), proposal);
        return proposal;
    }

    std::expected<Vote, ConsensusError> ConsensusEngine::accept_proposal(const Proposal& proposal) {
        std::lock_guard lock(mutex_);
        if (!initialized_ || !identity_.has_value()) {
            return std::unexpected(ConsensusError::NotValidator);
        }
        if (!verify_proposal(proposal)) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        if (proposal_validator_ && !proposal_validator_(proposal.header)) {
            return std::unexpected(ConsensusError::InvalidRoot);
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
        const auto persisted         = store_->persist_vote(vote, next_state);
        if (!persisted.has_value()) {
            return std::unexpected(persisted.error());
        }
        safety_state_              = std::move(next_state);
        const auto stored_proposal = store_->persist_proposal(proposal);
        if (!stored_proposal.has_value()) {
            return std::unexpected(stored_proposal.error());
        }
        proposals_.insert_or_assign(vote.header_hash, proposal);
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
        if (proposal_validator_ && !proposal_validator_(proposal.header)) {
            return std::unexpected(ConsensusError::InvalidRoot);
        }
        const auto persisted = store_->persist_proposal(proposal);
        if (!persisted.has_value()) {
            return std::unexpected(persisted.error());
        }
        proposals_.insert_or_assign(hash_header(proposal.header), proposal);
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
        const auto stored = store_->persist_certificate_state(certificate, next_state);
        if (!stored.has_value()) {
            return std::unexpected(stored.error());
        }
        certificates_.insert_or_assign(hash_certificate(certificate), certificate);
        certified_headers_.insert(certificate.header_hash);
        votes_.erase(certificate.header_hash);
        safety_state_ = std::move(next_state);
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
            return certificate.height == 0 && certificate.round == 0 && certificate.signatures.empty()
                   && certificate.signer_bitmap
                          == std::vector<std::uint8_t>((validators_.active().size() + 7) / 8, 0)
                   && certificate.header_hash == genesis_certificate().header_hash;
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

    QuorumCertificate ConsensusEngine::genesis_certificate() const {
        const auto payload =
            std::string(GenesisDomain) + validators_.document().network_id.to_string() + validators_.hash();
        return QuorumCertificate {
            .protocol_version = ProtocolVersion,
            .network_id       = validators_.document().network_id,
            .epoch            = validators_.document().epoch,
            .height           = 0,
            .round            = 0,
            .phase            = Phase::Genesis,
            .header_hash      = Utils::calculate_hash(payload, Utils::HashAlgorithm::Blake3),
            .signer_bitmap    = std::vector<std::uint8_t>((validators_.active().size() + 7) / 8, 0),
            .signatures       = {},
        };
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

    bool ConsensusEngine::is_local_leader(std::uint64_t height, std::uint64_t round) const {
        return identity_.has_value()
               && validators_.leader(height, round).validator_id == identity_.value().validator_id;
    }

    bool ConsensusEngine::verify_proposal(const Proposal& proposal) const {
        if (proposal.header.protocol_version != ProtocolVersion
            || proposal.header.network_id != validators_.document().network_id
            || proposal.header.epoch != validators_.document().epoch || proposal.header.height == 0
            || proposal.header.validator_set_hash != validators_.hash()
            || proposal.header.parent_certificate_hash != hash_certificate(proposal.parent_certificate)
            || proposal.header.height != proposal.parent_certificate.height + 1
            || proposal.header.section_root.empty() || proposal.header.transaction_root.empty()
            || proposal.header.state_commitment
                   != state_commitment(proposal.parent_certificate,
                                       proposal.header.section_root,
                                       proposal.header.transaction_root)
            || !verify_certificate(proposal.parent_certificate)) {
            return false;
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

    bool ConsensusEngine::safe_to_vote(const Proposal& proposal) const {
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
            .height           = grandparent->second.header.height,
            .dag_section      = grandparent->second.header.dag_section,
            .header_hash      = hash_header(grandparent->second.header),
            .section_root     = grandparent->second.header.section_root,
            .certificate_hash = hash_certificate(certificate),
        };
    }

    std::string ConsensusEngine::state_commitment(const QuorumCertificate& parent,
                                                  std::string_view         section_root,
                                                  std::string_view         transaction_root) const {
        return Utils::calculate_hash(std::string(StateDomain) + hash_certificate(parent)
                                         + std::string(section_root) + std::string(transaction_root)
                                         + validators_.hash(),
                                     Utils::HashAlgorithm::Blake3);
    }

    void ConsensusEngine::prune_memory(std::uint64_t finalized_height) {
        if (finalized_height <= 2) {
            return;
        }
        const auto minimum_height = finalized_height - 2;
        std::erase_if(proposals_, [minimum_height](const auto& item) {
            return item.second.header.height < minimum_height;
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
    }

} // namespace ExtraChain::Consensus
