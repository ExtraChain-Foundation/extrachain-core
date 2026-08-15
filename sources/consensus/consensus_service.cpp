/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/consensus_service.h"

#include <charconv>

#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "network/network_service.h"
#include "network/peer_meta.h"
#include "network/responder.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    ConsensusService::ConsensusService(Core::ExtraChainNode& node, std::filesystem::path directory)
        : node_(node)
        , directory_(std::move(directory)) {
    }

    ConsensusService::~ConsensusService() {
        deactivate();
    }

    std::expected<bool, ConsensusError> ConsensusService::activate(const ActorId& network_id) {
        std::lock_guard lock(mutex_);
        if (consensus_) {
            return true;
        }
        if (!std::filesystem::exists(directory_ / "validator-set.msgpack")) {
            return false;
        }
        auto loaded = ShadowConsensus::load(directory_, network_id, [this](const Proposal& proposal) {
            return validate_proposal(proposal);
        });
        if (!loaded.has_value()) {
            return std::unexpected(loaded.error());
        }
        consensus_            = std::move(loaded.value());
        const auto reconciled = reconcile_finalized_checkpoint();
        if (!reconciled.has_value()) {
            consensus_.reset();
            return std::unexpected(reconciled.error());
        }
        if (consensus_->engine().identity().has_value()) {
            const auto* local_validator =
                consensus_->engine().validators().find(consensus_->engine().identity().value().validator_id);
            if (local_validator == nullptr || local_validator->node_identifier != node_.node_identifier()) {
                consensus_.reset();
                return std::unexpected(ConsensusError::InvalidValidator);
            }
        }
        authenticator_ = std::make_unique<PeerAuthenticator>(consensus_->engine().validators(),
                                                             consensus_->engine().identity());
        timeout_task_  = Core::DeadlineTask::create(node_.runtime_executor(), [this] {
            timeout_elapsed();
        });
        connections_.emplace_back(node_.network()->socket_activated_event().subscribe(
            [this](const std::string&, const std::string& identifier) {
                peer_connected(identifier);
            }));
        connections_.emplace_back(node_.dag()->control_committed_event().subscribe([this](SectionId section) {
            std::uint64_t value  = 0;
            const auto    text   = section.to_string();
            const auto    parsed = std::from_chars(text.data(), text.data() + text.size(), value);
            if (parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size()) {
                node_.post_storage([this, value] {
                    checkpoint_ready(value);
                });
            }
        }));
        eInfo("[Consensus] Shadow certification enabled for epoch {} with {} validators",
              consensus_->engine().validators().document().epoch,
              consensus_->engine().validators().active().size());
        reset_timeout();
        queue_next_checkpoint();
        return true;
    }

    void ConsensusService::deactivate() {
        std::lock_guard lock(mutex_);
        connections_.clear();
        authenticator_.reset();
        if (timeout_task_) {
            timeout_task_->cancel();
        }
        timeout_task_.reset();
        consensus_.reset();
        latest_proposal_.reset();
        latest_certificate_.reset();
        latest_timeout_certificate_.reset();
        pending_checkpoints_.clear();
        pending_proposals_.clear();
    }

    void ConsensusService::receive_network_message(MessageType        type,
                                                   MessageStatus      status,
                                                   const std::string& serialized,
                                                   const Responder&   responder,
                                                   std::string_view   peer_identifier) {
        const auto meta = node_.network()->peer_meta_for(std::string(peer_identifier));
        if (!meta.has_value() || !meta.value().supports_shadow_consensus()) {
            return;
        }
        switch (type) {
        case MessageType::ConsensusChallenge: {
            const auto value = MessagePack::deserialize<AuthenticationChallenge>(serialized);
            if (status == MessageStatus::Request && value.has_value()) {
                receive_challenge(value.value(), responder);
            }
            break;
        }
        case MessageType::ConsensusAuthentication: {
            const auto value = MessagePack::deserialize<AuthenticationResponse>(serialized);
            if (status == MessageStatus::Response && value.has_value()) {
                receive_authentication(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusProposal: {
            const auto value = MessagePack::deserialize<Proposal>(serialized);
            if (status == MessageStatus::NoStatus && value.has_value()) {
                receive_proposal(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusVote: {
            const auto value = MessagePack::deserialize<Vote>(serialized);
            if (status == MessageStatus::NoStatus && value.has_value()) {
                receive_vote(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusCertificate: {
            const auto value = MessagePack::deserialize<QuorumCertificate>(serialized);
            if (status == MessageStatus::NoStatus && value.has_value()) {
                receive_certificate(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusTimeoutVote: {
            const auto value = MessagePack::deserialize<TimeoutVote>(serialized);
            if (status == MessageStatus::NoStatus && value.has_value()) {
                receive_timeout_vote(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusTimeoutCertificate: {
            const auto value = MessagePack::deserialize<TimeoutCertificate>(serialized);
            if (status == MessageStatus::NoStatus && value.has_value()) {
                receive_timeout_certificate(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusBatchRequest: {
            const auto value = MessagePack::deserialize<SectionBatchRequest>(serialized);
            if (status == MessageStatus::Request && value.has_value()) {
                receive_batch_request(value.value(), responder, peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusBatchData: {
            const auto value = MessagePack::deserialize<SectionBatchData>(serialized);
            if (status == MessageStatus::Response && value.has_value()) {
                receive_batch_data(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusSyncRequest: {
            const auto value = MessagePack::deserialize<ShadowSyncRequest>(serialized);
            if (status == MessageStatus::Request && value.has_value()) {
                receive_sync_request(value.value(), responder, peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusSyncResponse: {
            const auto value = MessagePack::deserialize<ShadowSyncResponse>(serialized);
            if (status == MessageStatus::Response && value.has_value()) {
                receive_sync_response(value.value(), peer_identifier);
            }
            break;
        }
        default:
            break;
        }
    }

    void ConsensusService::receive_challenge(const AuthenticationChallenge& challenge,
                                             const Responder&               responder) {
        std::lock_guard lock(mutex_);
        if (!authenticator_) {
            return;
        }
        const auto response = authenticator_->answer_challenge(challenge, node_.node_identifier());
        if (response.has_value()) {
            responder.send_response(response.value(),
                                    MessageType::ConsensusAuthentication,
                                    SendMode::Focused,
                                    MessageStatus::Response);
        }
    }

    void ConsensusService::receive_authentication(const AuthenticationResponse& response,
                                                  std::string_view              peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!authenticator_) {
            return;
        }
        const auto verified = authenticator_->verify_response(response, peer_identifier);
        if (!verified.has_value()) {
            eWarning("[Consensus] Validator authentication failed for peer {}", peer_identifier);
            return;
        }
        if (latest_proposal_.has_value()) {
            send_to_peer(latest_proposal_.value(),
                         MessageType::ConsensusProposal,
                         std::string(peer_identifier),
                         MessageStatus::NoStatus);
        }
        if (latest_certificate_.has_value()) {
            send_to_peer(latest_certificate_.value(),
                         MessageType::ConsensusCertificate,
                         std::string(peer_identifier),
                         MessageStatus::NoStatus);
        }
        if (latest_timeout_certificate_.has_value()) {
            send_to_peer(latest_timeout_certificate_.value(),
                         MessageType::ConsensusTimeoutCertificate,
                         std::string(peer_identifier),
                         MessageStatus::NoStatus);
        }
        send_to_peer(
            ShadowSyncRequest {
                .protocol_version = ProtocolVersion,
                .network_id       = consensus_->engine().validators().document().network_id,
                .epoch            = consensus_->engine().validators().document().epoch,
                .finalized_height = consensus_->engine().safety_state().finalized_height,
            },
            MessageType::ConsensusSyncRequest,
            std::string(peer_identifier),
            MessageStatus::Request);
    }

    void ConsensusService::receive_proposal(const Proposal& proposal, std::string_view peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !authenticator_
            || !authenticator_->is_authenticated(peer_identifier, proposal.proposer_id)) {
            return;
        }
        const auto observed = consensus_->engine().observe_proposal(proposal);
        if (!observed.has_value()) {
            eWarning("[Shadow] Proposal {} at height {} was rejected with error {}",
                     hash_header(proposal.header),
                     proposal.header.height,
                     std::to_underlying(observed.error()));
            return;
        }
        pending_proposals_.insert_or_assign(hash_header(proposal.header), proposal);
        const auto available = validate_proposal(proposal);
        if (!available.has_value()) {
            if (available.error() == ConsensusError::DataUnavailable) {
                request_batch(proposal, peer_identifier);
            } else {
                eWarning("[Shadow] Proposal {} contains invalid batch data", hash_header(proposal.header));
            }
            return;
        }
        vote_for_proposal(proposal, peer_identifier);
    }

    void ConsensusService::receive_vote(const Vote& vote, std::string_view peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !authenticator_
            || !authenticator_->is_authenticated(peer_identifier, vote.validator_id)) {
            return;
        }
        const auto accepted = consensus_->receive_vote(vote, peer_identifier);
        if (!accepted.has_value()) {
            return;
        }
        if (accepted.value().equivocation.has_value()) {
            eCritical("[Consensus] Double vote detected for validator {} at height {} round {}",
                      vote.validator_id,
                      vote.height,
                      vote.round);
        }
        if (accepted.value().certificate.has_value()) {
            const auto& certificate = accepted.value().certificate.value();
            if (apply_certificate(certificate)) {
                send_to_validators(certificate, MessageType::ConsensusCertificate);
            }
        }
    }

    void ConsensusService::receive_certificate(const QuorumCertificate& certificate,
                                               std::string_view         peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !authenticator_) {
            return;
        }
        if (peer_identifier != node_.node_identifier()
            && !authenticator_->authenticated_validator(peer_identifier).has_value()) {
            return;
        }
        apply_certificate(certificate);
    }

    void ConsensusService::receive_timeout_vote(const TimeoutVote& vote, std::string_view peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !authenticator_
            || !authenticator_->is_authenticated(peer_identifier, vote.validator_id)) {
            return;
        }
        const auto accepted = consensus_->receive_timeout_vote(vote, peer_identifier);
        if (!accepted.has_value()) {
            return;
        }
        if (accepted.value().equivocation.has_value()) {
            eCritical("[Shadow] Conflicting timeout votes from validator {} at height {} round {}",
                      vote.validator_id,
                      vote.height,
                      vote.round);
        }
        if (accepted.value().certificate.has_value()
            && apply_timeout_certificate(accepted.value().certificate.value())) {
            send_to_validators(accepted.value().certificate.value(), MessageType::ConsensusTimeoutCertificate);
        }
    }

    void ConsensusService::receive_timeout_certificate(const TimeoutCertificate& certificate,
                                                       std::string_view          peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !authenticator_
            || !authenticator_->authenticated_validator(peer_identifier).has_value()) {
            return;
        }
        apply_timeout_certificate(certificate);
    }

    void ConsensusService::receive_batch_request(const SectionBatchRequest& request,
                                                 const Responder&           responder,
                                                 std::string_view           peer_identifier) {
        std::lock_guard lock(mutex_);
        const auto      peer_meta = node_.network()->peer_meta_for(std::string(peer_identifier));
        if (!consensus_ || !authenticator_ || !peer_meta.has_value()
            || !peer_meta.value().supports_shadow_consensus()
            || !authenticator_->authenticated_validator(peer_identifier).has_value()
            || request.protocol_version != ProtocolVersion
            || request.network_id != consensus_->engine().validators().document().network_id
            || request.epoch != consensus_->engine().validators().document().epoch) {
            return;
        }
        const auto batch = consensus_->engine().batch_for(request.header_hash);
        if (batch.has_value()) {
            responder.send_response(batch.value(),
                                    MessageType::ConsensusBatchData,
                                    SendMode::Focused,
                                    MessageStatus::Response);
        }
    }

    void ConsensusService::receive_batch_data(const SectionBatchData& batch, std::string_view peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !authenticator_
            || !authenticator_->authenticated_validator(peer_identifier).has_value()) {
            return;
        }
        const auto proposal = pending_proposals_.find(batch.header_hash);
        if (proposal == pending_proposals_.end()) {
            return;
        }
        const auto valid = node_.dag()->validate_shadow_batch(proposal->second,
                                                              batch,
                                                              consensus_->configuration().maximum_batch_bytes);
        if (!valid.has_value()) {
            eWarning("[Shadow] Batch {} from {} failed validation with error {}",
                     batch.header_hash,
                     peer_identifier,
                     std::to_underlying(valid.error()));
            return;
        }
        const auto staged = consensus_->engine().stage_batch(batch);
        if (!staged.has_value()) {
            eWarning("[Shadow] Batch {} could not be stored before voting", batch.header_hash);
            return;
        }
        vote_for_proposal(proposal->second, peer_identifier);
    }

    void ConsensusService::receive_sync_request(const ShadowSyncRequest& request,
                                                const Responder&         responder,
                                                std::string_view         peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || request.protocol_version != ProtocolVersion
            || request.network_id != consensus_->engine().validators().document().network_id
            || request.epoch != consensus_->engine().validators().document().epoch) {
            return;
        }

        ShadowSyncResponse response {
            .protocol_version = ProtocolVersion,
            .network_id       = request.network_id,
            .epoch            = request.epoch,
        };
        const auto proofs =
            consensus_->engine().finality_proofs_after(request.finalized_height, MaximumShadowSyncProofs);
        if (!proofs.has_value()) {
            eWarning("[Shadow] Cannot load finality proofs for peer {}", peer_identifier);
            return;
        }
        std::uint64_t total_bytes = 0;
        for (const auto& proof : proofs.value()) {
            const auto header_hash = hash_header(proof.finalized_proposal.header);
            const auto batch       = consensus_->engine().batch_for(header_hash);
            if (!batch.has_value()
                || batch.value().manifest.payload_bytes > consensus_->configuration().maximum_batch_bytes
                || batch.value().manifest.payload_bytes > MaximumShadowSyncBytes
                || total_bytes > MaximumShadowSyncBytes - batch.value().manifest.payload_bytes) {
                break;
            }
            total_bytes += batch.value().manifest.payload_bytes;
            response.proofs.push_back(proof);
            response.batches.push_back(batch.value());
        }
        responder.send_response(response,
                                MessageType::ConsensusSyncResponse,
                                SendMode::Focused,
                                MessageStatus::Response);
    }

    void ConsensusService::receive_sync_response(const ShadowSyncResponse& response,
                                                 std::string_view          peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || response.protocol_version != ProtocolVersion
            || response.network_id != consensus_->engine().validators().document().network_id
            || response.epoch != consensus_->engine().validators().document().epoch
            || response.proofs.size() > MaximumShadowSyncProofs
            || response.batches.size() != response.proofs.size()) {
            return;
        }

        std::map<std::string, SectionBatchData> batches;
        std::uint64_t                           total_bytes = 0;
        for (const auto& batch : response.batches) {
            if (batch.manifest.payload_bytes > consensus_->configuration().maximum_batch_bytes
                || batch.manifest.payload_bytes > MaximumShadowSyncBytes
                || total_bytes > MaximumShadowSyncBytes - batch.manifest.payload_bytes) {
                return;
            }
            total_bytes += batch.manifest.payload_bytes;
            batches.insert_or_assign(batch.header_hash, batch);
        }

        auto expected_height = consensus_->engine().safety_state().finalized_height + 1;
        for (const auto& proof : response.proofs) {
            if (proof.finalized_proposal.header.height != expected_height) {
                eWarning("[Shadow] Non-contiguous finality proof from {}", peer_identifier);
                return;
            }
            if (!consensus_->engine().verify_finality_proof(proof)) {
                eWarning("[Shadow] Invalid finality proof from {}", peer_identifier);
                return;
            }
            for (const auto* proposal :
                 { &proof.finalized_proposal, &proof.child_proposal, &proof.grandchild_proposal }) {
                const auto observed = consensus_->engine().observe_proposal(*proposal);
                if (!observed.has_value()) {
                    eWarning("[Shadow] Invalid proposal in a finality proof from {}", peer_identifier);
                    return;
                }
            }

            const auto finalized_hash = hash_header(proof.finalized_proposal.header);
            const auto batch          = batches.find(finalized_hash);
            if (batch == batches.end()) {
                eWarning("[Shadow] Finality proof {} has no batch data", finalized_hash);
                return;
            }
            const auto valid = node_.dag()->validate_shadow_batch(proof.finalized_proposal,
                                                                  batch->second,
                                                                  consensus_->configuration().maximum_batch_bytes);
            if (!valid.has_value() || !consensus_->engine().stage_batch(batch->second).has_value()) {
                eWarning("[Shadow] Finality proof batch {} failed validation", finalized_hash);
                return;
            }

            for (const auto* certificate :
                 { &proof.child_proposal.parent_certificate, &proof.grandchild_proposal.parent_certificate }) {
                const auto accepted = consensus_->receive_certificate(*certificate);
                if (!accepted.has_value()) {
                    return;
                }
            }
            if (!apply_certificate(proof.decision_certificate)) {
                return;
            }
            ++expected_height;
        }
        reset_timeout();
        if (response.proofs.size() == MaximumShadowSyncProofs) {
            send_to_peer(
                ShadowSyncRequest {
                    .protocol_version = ProtocolVersion,
                    .network_id       = consensus_->engine().validators().document().network_id,
                    .epoch            = consensus_->engine().validators().document().epoch,
                    .finalized_height = consensus_->engine().safety_state().finalized_height,
                },
                MessageType::ConsensusSyncRequest,
                std::string(peer_identifier),
                MessageStatus::Request);
        }
    }

    bool ConsensusService::apply_certificate(const QuorumCertificate& certificate) {
        const auto finalized = consensus_->receive_certificate(certificate);
        if (!finalized.has_value()) {
            eWarning("[Consensus] Certificate {} at height {} was rejected with error {}",
                     hash_certificate(certificate),
                     certificate.height,
                     std::to_underlying(finalized.error()));
            return false;
        }
        latest_certificate_ = certificate;
        if (latest_proposal_.has_value()
            && certificate.header_hash == hash_header(latest_proposal_.value().header)) {
            pending_checkpoints_.erase(latest_proposal_.value().batch.last_section);
        }
        reset_timeout();
        if (finalized.value().has_value()) {
            const auto& checkpoint = finalized.value().value();
            if (consensus_->configuration().mode == ShadowMode::Finality
                && checkpoint.height >= consensus_->configuration().activation_height
                && checkpoint.dag_section >= consensus_->configuration().activation_dag_section) {
                const auto proposal = consensus_->engine().proposal_for(checkpoint.header_hash);
                const auto batch    = consensus_->engine().batch_for(checkpoint.header_hash);
                if (!proposal.has_value() || !batch.has_value()) {
                    eCritical("[Shadow] Finalized batch {} is unavailable", checkpoint.header_hash);
                    return false;
                }
                const auto installed =
                    node_.dag()->install_shadow_batch(proposal.value(),
                                                      batch.value(),
                                                      consensus_->configuration().maximum_batch_bytes);
                if (!installed.has_value()) {
                    eCritical("[Shadow] Finalized batch {} could not be installed: {}",
                              checkpoint.header_hash,
                              std::to_underlying(installed.error()));
                    return false;
                }
            }
            finalized_event_.publish(finalized.value().value());
            eInfo("[Shadow] Finalized height {} at DAG section {}",
                  finalized.value().value().height,
                  finalized.value().value().dag_section);
        }
        queue_next_checkpoint();
        return true;
    }

    std::expected<void, ConsensusError> ConsensusService::reconcile_finalized_checkpoint() {
        if (!consensus_ || consensus_->configuration().mode != ShadowMode::Finality) {
            return {};
        }
        const auto finalized_height = consensus_->engine().safety_state().finalized_height;
        if (finalized_height < consensus_->configuration().activation_height || finalized_height == 0) {
            return {};
        }
        const auto proofs = consensus_->engine().finality_proofs_after(finalized_height - 1, 1);
        if (!proofs.has_value() || proofs.value().size() != 1
            || proofs.value().front().finalized_proposal.header.height != finalized_height) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const auto& proposal = proofs.value().front().finalized_proposal;
        if (proposal.header.dag_section < consensus_->configuration().activation_dag_section) {
            return {};
        }
        const auto batch = consensus_->engine().batch_for(hash_header(proposal.header));
        if (!batch.has_value()) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        return node_.dag()->install_shadow_batch(proposal,
                                                 batch.value(),
                                                 consensus_->configuration().maximum_batch_bytes);
    }

    bool ConsensusService::apply_timeout_certificate(const TimeoutCertificate& certificate) {
        const auto accepted = consensus_->receive_timeout_certificate(certificate);
        if (!accepted.has_value()) {
            eWarning("[Shadow] Timeout certificate at height {} round {} was rejected with error {}",
                     certificate.height,
                     certificate.round,
                     std::to_underlying(accepted.error()));
            return false;
        }
        latest_timeout_certificate_ = certificate;
        reset_timeout();
        propose_checkpoint(certificate.round + 1);
        return true;
    }

    bool ConsensusService::active() const noexcept {
        std::lock_guard lock(mutex_);
        return consensus_ != nullptr;
    }

    bool ConsensusService::voting() const noexcept {
        std::lock_guard lock(mutex_);
        return consensus_ && consensus_->engine().identity().has_value();
    }

    bool ConsensusService::controls_section(std::uint64_t section) const {
        std::lock_guard lock(mutex_);
        return consensus_ && consensus_->configuration().mode == ShadowMode::Finality
               && section >= consensus_->configuration().activation_dag_section;
    }

    bool ConsensusService::repair_section(std::uint64_t section) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || consensus_->configuration().mode != ShadowMode::Finality
            || section < consensus_->configuration().activation_dag_section) {
            return false;
        }
        const auto proof = consensus_->engine().finality_proof_for_section(section);
        if (proof.has_value() && proof.value().has_value()) {
            const auto& proof_value = proof.value().value();
            const auto  header_hash = hash_header(proof_value.finalized_proposal.header);
            const auto  batch       = consensus_->engine().batch_for(header_hash);
            if (batch.has_value()) {
                const auto installed =
                    node_.dag()->install_shadow_batch(proof_value.finalized_proposal,
                                                      batch.value(),
                                                      consensus_->configuration().maximum_batch_bytes);
                if (installed.has_value()) {
                    eInfo("[Shadow] Repaired DAG section {} from finality proof {}", section, header_hash);
                    return true;
                }
            }
        }

        const ShadowSyncRequest request {
            .protocol_version = ProtocolVersion,
            .network_id       = consensus_->engine().validators().document().network_id,
            .epoch            = consensus_->engine().validators().document().epoch,
            .finalized_height = consensus_->engine().safety_state().finalized_height,
        };
        send_to_validators(request, MessageType::ConsensusSyncRequest, MessageStatus::Request);
        eWarning("[Shadow] DAG section {} waits for a finality proof", section);
        return false;
    }

    Core::Event<const FinalizedCheckpoint&>& ConsensusService::finalized_event() noexcept {
        return finalized_event_;
    }

    void ConsensusService::peer_connected(const std::string& identifier) {
        std::lock_guard lock(mutex_);
        if (!authenticator_) {
            return;
        }
        // A node identifier can reconnect on a new transport. The new connection must
        // complete its own challenge before it can carry consensus messages.
        authenticator_->forget_peer(identifier);
        const auto meta = node_.network()->peer_meta_for(identifier);
        if (!meta.has_value() || !meta.value().supports_shadow_consensus()) {
            return;
        }
        const auto challenge = authenticator_->create_challenge(node_.node_identifier(), identifier);
        if (challenge.has_value()) {
            Responder responder(node_.network());
            responder.add_identifier(identifier);
            node_.network()->send_message(challenge.value(),
                                          MessageType::ConsensusChallenge,
                                          SendMode::Focused,
                                          MessageStatus::Request,
                                          responder);
        }
        send_to_peer(
            ShadowSyncRequest {
                .protocol_version = ProtocolVersion,
                .network_id       = consensus_->engine().validators().document().network_id,
                .epoch            = consensus_->engine().validators().document().epoch,
                .finalized_height = consensus_->engine().safety_state().finalized_height,
            },
            MessageType::ConsensusSyncRequest,
            identifier,
            MessageStatus::Request);
    }

    void ConsensusService::checkpoint_ready(std::uint64_t section) {
        std::lock_guard lock(mutex_);
        if (!consensus_) {
            return;
        }
        const auto control = node_.dag()->read_control(SectionId(section));
        const auto first   = section == 0 ? SectionId(0) : SectionId(section) - CONTROL_INTERVAL_DIFF;
        const auto batch   = node_.dag()->build_shadow_batch(first, SectionId(section), {});
        if (!control.has_value() || !batch.has_value()
            || batch.value().manifest.payload_bytes > consensus_->configuration().maximum_batch_bytes) {
            return;
        }
        pending_checkpoints_.insert_or_assign(section,
                                              ShadowCheckpoint {
                                                  .batch        = batch.value().manifest,
                                                  .section_root = control.value().control,
                                              });
        propose_checkpoint(consensus_->engine().safety_state().current_round);
    }

    void ConsensusService::queue_next_checkpoint() {
        if (!consensus_ || !consensus_->engine().safety_state().highest_certificate.has_value()) {
            return;
        }
        const auto& highest = consensus_->engine().safety_state().highest_certificate.value();
        auto        target  = consensus_->configuration().activation_dag_section;
        if (highest.phase == Phase::Genesis) {
            target = target == 0 ? ShadowSectionInterval : target;
        } else {
            const auto parent = consensus_->engine().proposal_for(highest.header_hash);
            if (!parent.has_value()
                || parent.value().batch.last_section
                       > std::numeric_limits<std::uint64_t>::max() - ShadowSectionInterval) {
                return;
            }
            target = parent.value().batch.last_section + ShadowSectionInterval;
        }
        if (node_.dag()->read_control(SectionId(target)).has_value()) {
            checkpoint_ready(target);
        }
    }

    void ConsensusService::propose_checkpoint(std::uint64_t round) {
        if (!consensus_ || pending_checkpoints_.empty()) {
            return;
        }
        const auto highest = consensus_->engine().safety_state().highest_certificate;
        if (!highest.has_value()) {
            return;
        }
        std::uint64_t next_section = 0;
        if (highest.value().phase != Phase::Genesis) {
            const auto parent = consensus_->engine().proposal_for(highest.value().header_hash);
            if (!parent.has_value()
                || parent.value().batch.last_section == std::numeric_limits<std::uint64_t>::max()) {
                return;
            }
            next_section = parent.value().batch.last_section + 1;
        }
        while (!pending_checkpoints_.empty()
               && pending_checkpoints_.begin()->second.batch.last_section < next_section) {
            pending_checkpoints_.erase(pending_checkpoints_.begin());
        }
        if (pending_checkpoints_.empty()
            || (next_section != 0 && pending_checkpoints_.begin()->second.batch.first_section != next_section)) {
            return;
        }
        const auto proposal = consensus_->make_checkpoint_proposal(pending_checkpoints_.begin()->second, round);
        if (!proposal.has_value()) {
            eWarning("[Shadow] Cannot create proposal for round {}: {}",
                     round,
                     std::to_underlying(proposal.error()));
            return;
        }
        if (!proposal.value().has_value()) {
            return;
        }

        const auto& proposal_value = proposal.value().value();
        auto        batch          = node_.dag()->build_shadow_batch(SectionId(proposal_value.batch.first_section),
                                                     SectionId(proposal_value.batch.last_section),
                                                     hash_header(proposal_value.header));
        if (!batch.has_value()
            || !node_.dag()
                    ->validate_shadow_batch(proposal_value,
                                            batch.value(),
                                            consensus_->configuration().maximum_batch_bytes)
                    .has_value()
            || !consensus_->engine().stage_batch(batch.value()).has_value()) {
            eWarning("[Shadow] Leader could not stage the proposed batch");
            return;
        }
        const auto self_vote = consensus_->engine().accept_proposal(proposal_value);
        if (!self_vote.has_value()) {
            eWarning("[Shadow] Leader could not persist its vote");
            return;
        }
        const auto accepted = consensus_->engine().accept_vote(self_vote.value());
        if (!accepted.has_value()) {
            eWarning("[Shadow] Leader could not accept its vote");
            return;
        }
        latest_proposal_ = proposal.value();
        if (accepted.value().certificate.has_value() && apply_certificate(accepted.value().certificate.value())) {
            send_to_validators(accepted.value().certificate.value(), MessageType::ConsensusCertificate);
        }
        send_to_validators(proposal_value, MessageType::ConsensusProposal);
        reset_timeout();
    }

    void ConsensusService::request_batch(const Proposal& proposal, std::string_view peer_identifier) {
        SectionBatchRequest request {
            .protocol_version = ProtocolVersion,
            .network_id       = proposal.header.network_id,
            .epoch            = proposal.header.epoch,
            .header_hash      = hash_header(proposal.header),
        };
        send_to_peer(request,
                     MessageType::ConsensusBatchRequest,
                     std::string(peer_identifier),
                     MessageStatus::Request);
    }

    void ConsensusService::vote_for_proposal(const Proposal& proposal, std::string_view peer_identifier) {
        const auto vote = consensus_->engine().accept_proposal(proposal);
        if (!vote.has_value()) {
            eWarning("[Shadow] Proposal {} at height {} was not voted for: {}",
                     hash_header(proposal.header),
                     proposal.header.height,
                     std::to_underlying(vote.error()));
            return;
        }
        send_to_peer(vote.value(),
                     MessageType::ConsensusVote,
                     std::string(peer_identifier),
                     MessageStatus::NoStatus);
        pending_proposals_.erase(hash_header(proposal.header));
        reset_timeout();
    }

    void ConsensusService::timeout_elapsed() {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !consensus_->engine().identity().has_value()
            || !consensus_->engine().safety_state().highest_certificate.has_value()) {
            return;
        }
        const auto height = consensus_->engine().safety_state().highest_certificate.value().height + 1;
        const auto round  = consensus_->engine().safety_state().current_round;
        const auto vote   = consensus_->make_timeout_vote(height, round);
        if (!vote.has_value()) {
            reset_timeout();
            return;
        }
        const auto accepted = consensus_->engine().accept_timeout_vote(vote.value());
        if (accepted.has_value() && accepted.value().certificate.has_value()
            && apply_timeout_certificate(accepted.value().certificate.value())) {
            send_to_validators(accepted.value().certificate.value(), MessageType::ConsensusTimeoutCertificate);
        }
        send_to_validators(vote.value(), MessageType::ConsensusTimeoutVote);
        reset_timeout();
    }

    void ConsensusService::reset_timeout() {
        if (!timeout_task_ || !consensus_) {
            return;
        }
        const auto& configuration = consensus_->configuration();
        const auto  round         = consensus_->engine().safety_state().current_round;
        const auto  multiplier    = std::uint64_t(1) << std::min<std::uint64_t>(round, 10);
        const auto  base   = configuration.proposal_timeout_ms > configuration.maximum_timeout_ms / multiplier
                                 ? configuration.maximum_timeout_ms
                                 : configuration.proposal_timeout_ms * multiplier;
        const auto  jitter = std::hash<std::string> {}(node_.node_identifier()) % 251;
        timeout_task_->schedule_after(
            std::chrono::milliseconds(std::min(configuration.maximum_timeout_ms, base + jitter)));
    }

    void ConsensusService::send_to_peer(const auto&        payload,
                                        MessageType        message_type,
                                        const std::string& identifier,
                                        MessageStatus      status) {
        Responder responder(node_.network());
        responder.add_identifier(identifier);
        node_.network()->send_message(payload, message_type, SendMode::Focused, status, responder);
    }

    void ConsensusService::send_to_validators(const auto& payload, MessageType message_type) {
        send_to_validators(payload, message_type, MessageStatus::NoStatus);
    }

    void ConsensusService::send_to_validators(const auto&   payload,
                                              MessageType   message_type,
                                              MessageStatus status) {
        for (const auto& identifier :
             node_.network()->active_full_peers_with_capability(SHADOW_CONSENSUS_CAPABILITY)) {
            send_to_peer(payload, message_type, identifier, status);
        }
    }

    std::expected<void, ConsensusError> ConsensusService::validate_proposal(const Proposal& proposal) {
        if (!consensus_ || proposal.batch.payload_bytes > consensus_->configuration().maximum_batch_bytes) {
            return std::unexpected(ConsensusError::DataTooLarge);
        }
        const auto stored = consensus_->engine().batch_for(hash_header(proposal.header));
        if (stored.has_value()) {
            return node_.dag()->validate_shadow_batch(proposal,
                                                      stored.value(),
                                                      consensus_->configuration().maximum_batch_bytes);
        }

        auto local = node_.dag()->build_shadow_batch(SectionId(proposal.batch.first_section),
                                                     SectionId(proposal.batch.last_section),
                                                     hash_header(proposal.header));
        if (!local.has_value()) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        if (hash_batch_manifest(local.value().manifest) != proposal.header.batch_root) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        const auto valid = node_.dag()->validate_shadow_batch(proposal,
                                                              local.value(),
                                                              consensus_->configuration().maximum_batch_bytes);
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        return consensus_->engine().stage_batch(std::move(local.value()));
    }

} // namespace ExtraChain::Consensus
