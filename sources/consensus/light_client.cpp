/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/light_client.h"

#include <algorithm>
#include <bit>
#include <limits>
#include <set>

#include "utils/file_io.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        bool bit_is_set(const std::vector<std::uint8_t>& bitmap, std::size_t index) {
            return (bitmap[index / 8] & static_cast<std::uint8_t>(1U << (index % 8))) != 0;
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

        bool verify_certificate(const QuorumCertificate& certificate,
                                const ValidatorSetView&  validators,
                                const EpochBootstrapV1*  bootstrap = nullptr) {
            if (certificate.protocol_version != ProtocolVersion
                || certificate.network_id != validators.document().network_id
                || certificate.epoch != validators.document().epoch || certificate.header_hash.empty()) {
                return false;
            }
            if (certificate.phase == Phase::Genesis) {
                const auto expected = bootstrap == nullptr
                                          ? make_genesis_certificate(validators)
                                          : make_epoch_genesis_certificate(validators, *bootstrap);
                return certificate.height == expected.height && certificate.round == 0
                       && certificate.signatures.empty()
                       && certificate.signer_bitmap
                              == std::vector<std::uint8_t>((validators.active().size() + 7) / 8, 0)
                       && certificate.header_hash == expected.header_hash;
            }
            if (certificate.phase != Phase::Prepare
                || certificate.signer_bitmap.size() != (validators.active().size() + 7) / 8
                || bit_count(certificate.signer_bitmap) != certificate.signatures.size()
                || certificate.signatures.size() < validators.quorum()) {
                return false;
            }
            if (validators.active().size() % 8 != 0) {
                const auto valid_bits = static_cast<std::uint8_t>((1U << (validators.active().size() % 8)) - 1U);
                if ((certificate.signer_bitmap.back() & static_cast<std::uint8_t>(~valid_bits)) != 0) {
                    return false;
                }
            }
            std::size_t signature_index = 0;
            for (std::size_t index = 0; index < validators.active().size(); ++index) {
                if (!bit_is_set(certificate.signer_bitmap, index)) {
                    continue;
                }
                const auto& validator = validators.active()[index];
                const Vote  vote {
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
                if (!active_at(validator, vote.height)
                    || !verify_payload(validator.consensus_public_key,
                                       vote_signing_payload(vote),
                                       vote.signature)) {
                    return false;
                }
            }
            return true;
        }

        bool verify_timeout_certificate(const TimeoutCertificate& certificate,
                                        const ValidatorSetView&   validators,
                                        const EpochBootstrapV1*   bootstrap) {
            if (certificate.protocol_version != ProtocolVersion
                || certificate.network_id != validators.document().network_id
                || certificate.epoch != validators.document().epoch || certificate.height == 0
                || certificate.height != certificate.highest_certificate.height + 1
                || !verify_certificate(certificate.highest_certificate, validators, bootstrap)
                || certificate.signer_bitmap.size() != (validators.active().size() + 7) / 8
                || bit_count(certificate.signer_bitmap) != certificate.signatures.size()
                || certificate.signatures.size() < validators.quorum()) {
                return false;
            }
            if (validators.active().size() % 8 != 0) {
                const auto valid_bits = static_cast<std::uint8_t>((1U << (validators.active().size() % 8)) - 1U);
                if ((certificate.signer_bitmap.back() & static_cast<std::uint8_t>(~valid_bits)) != 0) {
                    return false;
                }
            }
            std::size_t signature_index = 0;
            for (std::size_t index = 0; index < validators.active().size(); ++index) {
                if (!bit_is_set(certificate.signer_bitmap, index)) {
                    continue;
                }
                const auto&       validator = validators.active()[index];
                const TimeoutVote vote {
                    .protocol_version         = certificate.protocol_version,
                    .network_id               = certificate.network_id,
                    .epoch                    = certificate.epoch,
                    .height                   = certificate.height,
                    .round                    = certificate.round,
                    .highest_certificate_hash = hash_certificate(certificate.highest_certificate),
                    .validator_id             = validator.validator_id,
                    .signature                = certificate.signatures[signature_index++],
                };
                if (!active_at(validator, vote.height)
                    || !verify_payload(validator.consensus_public_key,
                                       timeout_vote_signing_payload(vote),
                                       vote.signature)) {
                    return false;
                }
            }
            return true;
        }

        bool verify_proposal(const Proposal&         proposal,
                             const ValidatorSetView& validators,
                             const EpochBootstrapV1* bootstrap) {
            if (proposal.header.protocol_version != ProtocolVersion
                || proposal.header.network_id != validators.document().network_id
                || proposal.header.epoch != validators.document().epoch || proposal.header.height == 0
                || proposal.header.validator_set_hash != validators.hash()
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
                || proposal.state.validator_set_hash != validators.hash()
                || proposal.state.account_state_root.empty() || proposal.state.contract_state_root.empty()
                || proposal.state.token_registry_root.empty()
                || proposal.header.state_commitment != hash_state_commitment(proposal.state)
                || !verify_certificate(proposal.parent_certificate, validators, bootstrap)) {
                return false;
            }
            if ((proposal.header.round == 0 && proposal.timeout_certificate.has_value())
                || (proposal.header.round != 0
                    && (!proposal.timeout_certificate.has_value()
                        || !verify_timeout_certificate(proposal.timeout_certificate.value(), validators, bootstrap)
                        || proposal.timeout_certificate.value().height != proposal.header.height
                        || proposal.timeout_certificate.value().round + 1 != proposal.header.round
                        || hash_certificate(proposal.timeout_certificate.value().highest_certificate)
                               != hash_certificate(proposal.parent_certificate)))) {
                return false;
            }
            if (proposal.parent_certificate.phase == Phase::Genesis) {
                if (bootstrap != nullptr
                    && (proposal.batch.first_section != bootstrap->first_dag_section
                        || proposal.batch.previous_section_root != bootstrap->previous_section_root)) {
                    return false;
                }
                if (bootstrap == nullptr && proposal.batch.first_section == 0
                    && !proposal.batch.previous_section_root.empty()) {
                    return false;
                }
                const auto expected_previous =
                    bootstrap == nullptr ? std::string {} : bootstrap->previous_state_commitment;
                if (proposal.state.previous_state_commitment != expected_previous) {
                    return false;
                }
            }
            const auto* proposer = validators.find(proposal.proposer_id);
            return proposer != nullptr && active_at(*proposer, proposal.header.height)
                   && validators.leader(proposal.header.height, proposal.header.round).validator_id
                          == proposal.proposer_id
                   && verify_payload(proposer->consensus_public_key,
                                     proposal_signing_payload(proposal),
                                     proposal.signature);
        }

        bool extends_batch(const Proposal& parent, const Proposal& child) {
            return parent.batch.last_section != std::numeric_limits<std::uint64_t>::max()
                   && parent.batch.last_section + 1 == child.batch.first_section
                   && parent.header.section_root == child.batch.previous_section_root;
        }

        bool verify_finality(const FinalityProof&    proof,
                             const ValidatorSetView& validators,
                             const EpochBootstrapV1* bootstrap) {
            const auto& first  = proof.finalized_proposal;
            const auto& second = proof.child_proposal;
            const auto& third  = proof.grandchild_proposal;
            return verify_proposal(first, validators, bootstrap) && verify_proposal(second, validators, bootstrap)
                   && verify_proposal(third, validators, bootstrap)
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
                   && verify_certificate(second.parent_certificate, validators, bootstrap)
                   && verify_certificate(third.parent_certificate, validators, bootstrap)
                   && verify_certificate(proof.decision_certificate, validators, bootstrap);
        }
    } // namespace

    LightClientVerifier::LightClientVerifier(ValidatorSetView                trusted_set,
                                             std::optional<EpochBootstrapV1> bootstrap)
        : active_(std::move(trusted_set))
        , active_bootstrap_(std::move(bootstrap))
        , trusted_epoch_(active_.document().epoch) {
    }

    std::expected<LightClientVerifier, ConsensusError> LightClientVerifier::create(
        ValidatorSet                    trusted_set,
        std::optional<EpochBootstrapV1> bootstrap) {
        auto validators = ValidatorSetView::create(std::move(trusted_set));
        if (!validators.has_value()) {
            return std::unexpected(validators.error());
        }
        if (bootstrap.has_value() && !verify_epoch_bootstrap(bootstrap.value(), validators.value().document())) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        return LightClientVerifier(std::move(validators.value()), std::move(bootstrap));
    }

    std::expected<LightClientVerifier, ConsensusError> LightClientVerifier::bootstrap(TrustAnchorV1 anchor) {
        if (!verify_trust_anchor(anchor)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        auto verifier = create(anchor.initial_validators);
        if (!verifier.has_value()) {
            return std::unexpected(verifier.error());
        }
        verifier.value().trust_anchor_ = std::move(anchor);
        verifier.value().freshness_    = BootstrapFreshness::Unknown;
        return verifier;
    }

    std::expected<LightClientVerifier, ConsensusError> LightClientVerifier::restore(LightClientStateV1 state) {
        if (state.protocol_version != ProtocolVersion || state.network_id != state.active_validators.network_id
            || state.trusted_epoch != state.active_validators.epoch
            || (state.trusted_height == 0) != state.trusted_header_hash.empty()) {
            return std::unexpected(ConsensusError::InvalidProof);
        }
        if (!state.trust_anchor.has_value()
            && (!state.epoch_history.empty() || state.freshness == BootstrapFreshness::Confirmed)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        auto verifier = [&]() -> std::expected<LightClientVerifier, ConsensusError> {
            if (!state.trust_anchor.has_value()) {
                return create(state.active_validators, state.active_bootstrap);
            }
            auto anchored = bootstrap(state.trust_anchor.value());
            if (!anchored.has_value()) {
                return anchored;
            }
            std::size_t offset = 0;
            while (offset < state.epoch_history.size()) {
                const auto count = std::min(MaximumBootstrapEntries, state.epoch_history.size() - offset);
                BootstrapHistoryPageV1 page {
                    .network_id        = state.network_id,
                    .trust_anchor_hash = hash_trust_anchor(state.trust_anchor.value()),
                    .after_epoch       = anchored.value().active_.document().epoch,
                    .entries           = std::vector<EpochStartV1>(state.epoch_history.begin() + offset,
                                                         state.epoch_history.begin() + offset + count),
                };
                offset += count;
                if (offset < state.epoch_history.size()) {
                    page.next_after_epoch = page.entries.back().validators.epoch;
                }
                if (!anchored.value().apply_history_page(page).has_value()) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
            }
            const bool same_bootstrap =
                state.active_bootstrap.has_value() == anchored.value().active_bootstrap_.has_value()
                && (!state.active_bootstrap.has_value()
                    || hash_epoch_bootstrap(state.active_bootstrap.value())
                           == hash_epoch_bootstrap(anchored.value().active_bootstrap_.value()));
            if (hash_validator_set(state.active_validators)
                    != hash_validator_set(anchored.value().active_.document())
                || !same_bootstrap || state.trusted_height < anchored.value().trusted_height_
                || (state.trusted_height == anchored.value().trusted_height_
                    && !anchored.value().trusted_header_hash_.empty()
                    && state.trusted_header_hash != anchored.value().trusted_header_hash_)) {
                return std::unexpected(ConsensusError::InvalidProof);
            }
            return anchored;
        }();
        if (!verifier.has_value()) {
            return std::unexpected(verifier.error());
        }
        if (state.pending_epoch.has_value() != state.pending_policy.has_value()) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        if (state.pending_epoch.has_value()) {
            const auto& transition = state.pending_epoch.value();
            const auto  scheduled  = verifier.value().schedule_epoch(transition.change,
                                                                   transition.next_validators,
                                                                   state.pending_policy.value(),
                                                                   transition.proof,
                                                                   state.pending_minimum_sequence);
            if (!scheduled.has_value() || !verifier.value().pending_bootstrap_.has_value()
                || hash_epoch_bootstrap(verifier.value().pending_bootstrap_.value())
                       != hash_epoch_bootstrap(transition.bootstrap)) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
        }
        verifier.value().trusted_epoch_       = state.trusted_epoch;
        verifier.value().trusted_height_      = state.trusted_height;
        verifier.value().trusted_header_hash_ = std::move(state.trusted_header_hash);
        if (state.trust_anchor.has_value()
            && (!verify_trust_anchor(state.trust_anchor.value())
                || state.trust_anchor.value().network_id != verifier.value().active_.document().network_id)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        verifier.value().trust_anchor_  = std::move(state.trust_anchor);
        verifier.value().epoch_history_ = std::move(state.epoch_history);
        verifier.value().freshness_     = BootstrapFreshness::Unknown;
        return verifier;
    }

    std::expected<LightClientVerifier, ConsensusError> LightClientVerifier::load(
        const std::filesystem::path& path) {
        const auto bytes = FileIo::read_all(path);
        if (!bytes.has_value()) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        auto state = MessagePack::deserialize<LightClientStateV1>(bytes.value());
        if (!state.has_value()) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return restore(std::move(state.value()));
    }

    bool LightClientVerifier::verify_finality_proof(const FinalityProof& proof) const {
        const auto* validators =
            validators_for(proof.finalized_proposal.header.epoch, proof.finalized_proposal.header.height);
        return validators != nullptr
               && verify_finality(proof, *validators, bootstrap_for(proof.finalized_proposal.header.epoch));
    }

    std::expected<void, ConsensusError> LightClientVerifier::advance(const FinalityProof& proof) {
        if (!verify_finality_proof(proof)) {
            return std::unexpected(ConsensusError::InvalidProof);
        }
        const auto height      = proof.finalized_proposal.header.height;
        const auto header_hash = hash_header(proof.finalized_proposal.header);
        if (height < trusted_height_
            || (height == trusted_height_ && !trusted_header_hash_.empty()
                && trusted_header_hash_ != header_hash)) {
            return std::unexpected(ConsensusError::InvalidHeight);
        }
        if (height == trusted_height_ && trusted_header_hash_ == header_hash) {
            return {};
        }
        if (pending_.has_value() && pending_change_.has_value()
            && proof.finalized_proposal.header.epoch == pending_.value().document().epoch
            && proof.finalized_proposal.header.height >= pending_change_.value().activation_height) {
            active_           = std::move(pending_.value());
            active_bootstrap_ = std::move(pending_bootstrap_);
            pending_.reset();
            pending_change_.reset();
            pending_bootstrap_.reset();
            pending_transition_.reset();
            pending_policy_.reset();
            pending_minimum_sequence_ = 0;
        }
        trusted_epoch_       = proof.finalized_proposal.header.epoch;
        trusted_height_      = height;
        trusted_header_hash_ = header_hash;
        return {};
    }

    bool LightClientVerifier::verify_transaction_proof(const TransactionInclusionProofV1& proof) const {
        const auto* validators = validators_for(proof.epoch, proof.height);
        const auto& proposal   = proof.finality_proof.finalized_proposal;
        return validators != nullptr && proof.protocol_version == ProtocolVersion
               && proof.network_id == validators->document().network_id
               && proof.epoch == validators->document().epoch && proof.height == proposal.header.height
               && !proof.transaction_hash.empty()
               && verify_finality(proof.finality_proof, *validators, bootstrap_for(proof.epoch))
               && proposal.header.transaction_root == proposal.batch.transaction_root
               && verify_merkle_proof(proof.transaction_hash, proof.merkle_proof, proposal.batch.transaction_root);
    }

    std::expected<void, ConsensusError> LightClientVerifier::schedule_epoch(
        const EpochChangeV1&               change,
        ValidatorSet                       next_set,
        const MultisigPolicy&              policy,
        const TransactionInclusionProofV1& proof,
        std::uint64_t                      minimum_sequence) {
        if (pending_.has_value() || change.current_epoch != active_.document().epoch
            || change.current_validator_set_hash != active_.hash()
            || proof.transaction_hash != epoch_change_action_hash(change)
            || proof.height >= change.activation_height
            || (active_bootstrap_.has_value() && proof.height < active_bootstrap_.value().activation_height)
            || !verify_transaction_proof(proof)) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        auto next = ValidatorSetView::create_epoch_transition(std::move(next_set),
                                                              change,
                                                              policy,
                                                              proof.height,
                                                              minimum_sequence);
        if (!next.has_value()) {
            return std::unexpected(next.error());
        }
        const auto bootstrap = make_epoch_bootstrap(change, next.value().document(), proof);
        if (!bootstrap.has_value()) {
            return std::unexpected(bootstrap.error());
        }
        pending_            = std::move(next.value());
        pending_change_     = change;
        pending_bootstrap_  = bootstrap.value();
        pending_transition_ = EpochTransitionV1 {
            .change          = change,
            .next_validators = pending_.value().document(),
            .proof           = proof,
            .bootstrap       = bootstrap.value(),
        };
        pending_policy_           = policy;
        pending_minimum_sequence_ = minimum_sequence;
        return {};
    }

    const ValidatorSetView& LightClientVerifier::active_validators() const noexcept {
        return active_;
    }

    const std::optional<ValidatorSetView>& LightClientVerifier::pending_validators() const noexcept {
        return pending_;
    }

    LightClientStateV1 LightClientVerifier::snapshot() const {
        return LightClientStateV1 {
            .network_id               = active_.document().network_id,
            .active_validators        = active_.document(),
            .active_bootstrap         = active_bootstrap_,
            .pending_epoch            = pending_transition_,
            .pending_policy           = pending_policy_,
            .pending_minimum_sequence = pending_minimum_sequence_,
            .trusted_epoch            = trusted_epoch_,
            .trusted_height           = trusted_height_,
            .trusted_header_hash      = trusted_header_hash_,
            .trust_anchor             = trust_anchor_,
            .epoch_history            = epoch_history_,
            .freshness                = freshness_,
        };
    }

    std::expected<void, ConsensusError> LightClientVerifier::save(const std::filesystem::path& path) const {
        const auto written = FileIo::write_atomic(path, MessagePack::serialize(snapshot()));
        return written.has_value() ? std::expected<void, ConsensusError> {}
                                   : std::unexpected(ConsensusError::StorageFailure);
    }

    std::uint64_t LightClientVerifier::trusted_height() const noexcept {
        return trusted_height_;
    }

    std::expected<void, ConsensusError> LightClientVerifier::apply_history_page(
        const BootstrapHistoryPageV1& page) {
        auto candidate = *this;
        auto applied   = candidate.apply_history_page_in_place(page);
        if (!applied.has_value()) {
            return std::unexpected(applied.error());
        }
        *this = std::move(candidate);
        return {};
    }

    std::expected<void, ConsensusError> LightClientVerifier::apply_history_page_in_place(
        const BootstrapHistoryPageV1& page) {
        if (!trust_anchor_.has_value() || page.protocol_version != ProtocolVersion
            || page.network_id != trust_anchor_.value().network_id
            || page.trust_anchor_hash != hash_trust_anchor(trust_anchor_.value())
            || page.after_epoch != active_.document().epoch || page.entries.size() > MaximumBootstrapEntries
            || MessagePack::serialize(page).size() > MaximumBootstrapBytes) {
            return std::unexpected(ConsensusError::InvalidProof);
        }

        auto governance_sequence = std::uint64_t { 1 };
        auto recovery_sequence   = std::uint64_t { 1 };
        for (const auto& prior : epoch_history_) {
            if (prior.normal_transition.has_value()) {
                governance_sequence = std::max(governance_sequence,
                                               prior.normal_transition.value().change.authorization.sequence + 1);
            }
            if (prior.recovery.has_value()) {
                recovery_sequence = std::max(recovery_sequence, prior.recovery.value().recovery_sequence + 1);
            }
        }

        for (const auto& start : page.entries) {
            if (start.protocol_version != ProtocolVersion || start.validators.network_id != page.network_id
                || !verify_epoch_bootstrap(start.bootstrap, start.validators)
                || start.bootstrap.previous_epoch != active_.document().epoch) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            if (start.kind == EpochStartKind::Normal) {
                if (!start.normal_transition.has_value() || start.recovery.has_value()) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                const auto& transition = start.normal_transition.value();
                const auto  scheduled  = schedule_epoch(transition.change,
                                                      transition.next_validators,
                                                      trust_anchor_.value().governance_policy,
                                                      transition.proof,
                                                      governance_sequence);
                if (!scheduled.has_value() || !pending_.has_value() || !pending_bootstrap_.has_value()
                    || hash_validator_set(start.validators) != pending_.value().hash()
                    || hash_epoch_bootstrap(start.bootstrap) != hash_epoch_bootstrap(pending_bootstrap_.value())) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                const auto& finalized = transition.proof.finality_proof.finalized_proposal;
                if (finalized.header.height < trusted_height_
                    || (finalized.header.height == trusted_height_ && !trusted_header_hash_.empty()
                        && trusted_header_hash_ != hash_header(finalized.header))) {
                    return std::unexpected(ConsensusError::InvalidHeight);
                }
                trusted_height_      = finalized.header.height;
                trusted_header_hash_ = hash_header(finalized.header);
                active_              = std::move(pending_.value());
                active_bootstrap_    = std::move(pending_bootstrap_);
                pending_.reset();
                pending_change_.reset();
                pending_bootstrap_.reset();
                pending_transition_.reset();
                pending_policy_.reset();
                pending_minimum_sequence_ = 0;
                governance_sequence       = transition.change.authorization.sequence + 1;
            } else if (start.kind == EpochStartKind::Recovery) {
                if (start.normal_transition.has_value() || !start.recovery.has_value()) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                const auto& recovery = start.recovery.value();
                if (recovery.finalized_height < trusted_height_
                    || (recovery.finalized_height == trusted_height_ && !trusted_header_hash_.empty()
                        && recovery.finalized_header_hash != trusted_header_hash_)
                    || !verify_recovery_document(recovery,
                                                 trust_anchor_.value().recovery_policy,
                                                 active_.document(),
                                                 start.validators,
                                                 recovery_sequence,
                                                 recovery.finalized_height,
                                                 recovery.finalized_header_hash,
                                                 recovery.finalized_state_commitment)
                    || start.bootstrap.previous_finalized_height != recovery.finalized_height
                    || start.bootstrap.previous_state_commitment != recovery.finalized_state_commitment
                    || start.bootstrap.epoch_change_hash != recovery_action_hash(recovery)) {
                    return std::unexpected(ConsensusError::InvalidGovernance);
                }
                auto next = ValidatorSetView::create_recovery_transition(start.validators, recovery.operators);
                if (!next.has_value()) {
                    return std::unexpected(next.error());
                }
                active_              = std::move(next.value());
                active_bootstrap_    = start.bootstrap;
                trusted_height_      = recovery.finalized_height;
                trusted_header_hash_ = recovery.finalized_header_hash;
                recovery_sequence    = recovery.recovery_sequence + 1;
            } else {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            trusted_epoch_ = active_.document().epoch;
            epoch_history_.push_back(start);
        }
        if (page.next_after_epoch.has_value() && page.next_after_epoch.value() != active_.document().epoch) {
            return std::unexpected(ConsensusError::BootstrapIncomplete);
        }
        freshness_ = BootstrapFreshness::Unknown;
        return {};
    }

    std::expected<void, ConsensusError> LightClientVerifier::confirm_freshness(
        const std::vector<PeerFreshnessObservation>& observations) {
        std::set<std::string> peers;
        std::size_t           matches = 0;
        for (const auto& observation : observations) {
            if (observation.peer_identifier.empty() || !peers.insert(observation.peer_identifier).second) {
                return std::unexpected(ConsensusError::InvalidProof);
            }
            if (observation.epoch == trusted_epoch_ && observation.height == trusted_height_
                && observation.header_hash == trusted_header_hash_) {
                ++matches;
            }
        }
        if (peers.size() < 3 || matches < 2) {
            return std::unexpected(ConsensusError::InvalidProof);
        }
        freshness_ = BootstrapFreshness::Confirmed;
        return {};
    }

    BootstrapFreshness LightClientVerifier::freshness() const noexcept {
        return freshness_;
    }

    const ValidatorSetView* LightClientVerifier::validators_for(std::uint64_t epoch,
                                                                std::uint64_t height) const noexcept {
        if (pending_.has_value() && pending_change_.has_value() && epoch == pending_.value().document().epoch
            && height >= pending_change_.value().activation_height) {
            return &pending_.value();
        }
        if (epoch == active_.document().epoch
            && (!active_bootstrap_.has_value() || height >= active_bootstrap_.value().activation_height)
            && (!pending_change_.has_value() || height < pending_change_.value().activation_height)) {
            return &active_;
        }
        return nullptr;
    }

    const EpochBootstrapV1* LightClientVerifier::bootstrap_for(std::uint64_t epoch) const noexcept {
        if (pending_.has_value() && pending_bootstrap_.has_value() && pending_.value().document().epoch == epoch) {
            return &pending_bootstrap_.value();
        }
        return active_bootstrap_.has_value() && active_.document().epoch == epoch ? &active_bootstrap_.value()
                                                                                  : nullptr;
    }

} // namespace ExtraChain::Consensus
