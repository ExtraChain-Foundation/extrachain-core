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
#include <ranges>

#include "chain/actor_index.h"
#include "chain/dag.h"
#include "contracts/contract_transaction.h"
#include "contracts/standard_token.h"
#include "core/extrachain_node.h"
#include "managers/token_manager.h"
#include "network/network_service.h"
#include "network/peer_meta.h"
#include "network/responder.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        template <typename Map>
        std::vector<std::pair<std::string, std::string>> state_entries(const Map& values) {
            return { values.begin(), values.end() };
        }

        std::string contract_state_value(const ExtraChain::Contracts::ContractSummary& summary) {
            return MessagePack::serialize(std::tuple { summary.owner_id,
                                                       summary.kind,
                                                       summary.language,
                                                       summary.version,
                                                       summary.revision,
                                                       summary.module_hash,
                                                       summary.state_hash,
                                                       summary.transaction_hash,
                                                       summary.section });
        }

        std::string contract_state_value(std::string_view               owner_id,
                                         const ContractTransactionData& metadata,
                                         std::string_view               transaction_hash,
                                         std::uint64_t                  section) {
            return MessagePack::serialize(std::tuple { std::string(owner_id),
                                                       metadata.kind,
                                                       metadata.language,
                                                       metadata.version,
                                                       metadata.revision,
                                                       metadata.module_hash,
                                                       metadata.state_hash,
                                                       std::string(transaction_hash),
                                                       section });
        }

        std::string contract_state_value(std::string_view              owner_id,
                                         const ContractTransitionData& transition,
                                         std::string_view              transaction_hash,
                                         std::uint64_t                 section) {
            return MessagePack::serialize(std::tuple { std::string(owner_id),
                                                       transition.kind,
                                                       transition.language,
                                                       transition.version,
                                                       transition.revision,
                                                       transition.module_hash,
                                                       transition.state_hash,
                                                       std::string(transaction_hash),
                                                       section });
        }

        std::string token_registry_state_value(std::string_view owner_id,
                                               std::string_view contract_id,
                                               std::string_view kind,
                                               std::string_view language,
                                               std::string_view transaction_hash,
                                               std::uint64_t    section) {
            return MessagePack::serialize(std::tuple { std::string(owner_id),
                                                       std::string(contract_id),
                                                       std::string(kind),
                                                       std::string(language),
                                                       std::string(transaction_hash),
                                                       section });
        }

        std::string token_registry_state_value(const TokenData& token) {
            return token_registry_state_value(token.owner_id.to_string(),
                                              token.smart,
                                              token.kind,
                                              token.language,
                                              token.tx_hash.value_or(std::string {}),
                                              static_cast<std::uint64_t>(
                                                  token.section_id.value_or(SectionId(0)).to_int().value_or(0)));
        }

        std::uint64_t wall_clock_millis() {
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::system_clock::now().time_since_epoch())
                                                  .count());
        }
    } // namespace

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
        const auto& configuration = loaded.value()->configuration();
        const auto& safety        = loaded.value()->engine().safety_state();
        if (configuration.mode == ShadowMode::Finality
            && safety.finalized_height < configuration.activation_height) {
            if (configuration.activation_dag_section < ShadowSectionInterval) {
                return std::unexpected(ConsensusError::InvalidHeight);
            }
            const auto boundary = SectionId(configuration.activation_dag_section - ShadowSectionInterval);
            const auto state    = node_.dag()->state_projection();
            if (node_.dag()->current_section() != boundary || state.status != StateProjectionStatus::Ready
                || state.verified_section < boundary || !node_.dag()->read_control(boundary).has_value()) {
                return std::unexpected(ConsensusError::BootstrapIncomplete);
            }
        }
        consensus_    = std::move(loaded.value());
        intent_store_ = std::make_unique<IntentStore>(directory_ / "intent-pool.sqlite");
        if (!intent_store_->open().has_value()) {
            consensus_.reset();
            intent_store_.reset();
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto committed_nonces = intent_store_->load_committed_nonces();
        if (!committed_nonces.has_value()) {
            consensus_.reset();
            intent_store_.reset();
            return std::unexpected(committed_nonces.error());
        }
        const auto applied_checkpoint = intent_store_->load_applied_checkpoint();
        if (!applied_checkpoint.has_value()) {
            consensus_.reset();
            intent_store_.reset();
            return std::unexpected(applied_checkpoint.error());
        }
        committed_nonces_     = committed_nonces.value();
        applied_checkpoint_   = applied_checkpoint.value();
        const auto reconciled = reconcile_finalized_checkpoint();
        if (!reconciled.has_value()) {
            consensus_.reset();
            intent_store_.reset();
            return std::unexpected(reconciled.error());
        }
        const auto epoch_activated = consensus_->activate_scheduled_epoch();
        if (!epoch_activated.has_value()) {
            consensus_.reset();
            intent_store_.reset();
            return std::unexpected(epoch_activated.error());
        }
        if (consensus_->engine().identity().has_value()) {
            const auto* local_validator =
                consensus_->engine().validators().find(consensus_->engine().identity().value().validator_id);
            if (local_validator == nullptr || local_validator->node_identifier != node_.node_identifier()) {
                consensus_.reset();
                intent_store_.reset();
                return std::unexpected(ConsensusError::InvalidValidator);
            }
        }
        const auto pending_intents = intent_store_->load_pending();
        if (!pending_intents.has_value()) {
            consensus_.reset();
            intent_store_.reset();
            return std::unexpected(pending_intents.error());
        }
        std::vector<std::string> expired_intents;
        for (const auto& envelope : pending_intents.value()) {
            const auto actor = node_.actor_index()->read_actor(envelope.intent.sender, ActorGetType::NoRequest);
            if (!actor.has_value()) {
                eWarning("[Shadow] Pending intent {} waits for sender data", hash_intent(envelope.intent));
                continue;
            }
            const auto accepted = intent_pool_.submit(envelope,
                                                      Utils::to_base64(actor.value().key().public_key()),
                                                      committed_nonces_[envelope.intent.sender],
                                                      intent_height());
            if (!accepted.has_value() && accepted.error() == ConsensusError::IntentExpired) {
                expired_intents.push_back(hash_intent(envelope.intent));
            } else if (!accepted.has_value()) {
                consensus_.reset();
                intent_store_.reset();
                intent_pool_ = IntentPool {};
                return std::unexpected(accepted.error());
            } else if (!intent_store_->put(envelope).has_value()) {
                consensus_.reset();
                intent_store_.reset();
                intent_pool_ = IntentPool {};
                return std::unexpected(ConsensusError::StorageFailure);
            }
        }
        if (!expired_intents.empty()) {
            if (!intent_store_->expire(expired_intents).has_value()) {
                consensus_.reset();
                intent_store_.reset();
                intent_pool_ = IntentPool {};
                return std::unexpected(ConsensusError::StorageFailure);
            }
        }
        authenticator_ = std::make_unique<PeerAuthenticator>(consensus_->engine().validators(),
                                                             consensus_->engine().identity());
        voting_enabled_ =
            consensus_->engine().identity().has_value() && !consensus_->pending_recovery().has_value();
        timeout_task_      = Core::DeadlineTask::create(node_.runtime_executor(), [this] {
            timeout_elapsed();
        });
        recovery_task_     = Core::DeadlineTask::create(node_.runtime_executor(), [this] {
            std::lock_guard lock(mutex_);
            const auto      activated = activate_pending_recovery(wall_clock_millis());
            if (!activated.has_value()) {
                eCritical("[Shadow] Scheduled recovery could not be activated");
            }
        });
        intent_batch_task_ = Core::DeadlineTask::create(node_.runtime_executor(), [this] {
            std::lock_guard lock(mutex_);
            if (!latest_proposal_.has_value() || latest_proposal_.value().header.height != intent_height()) {
                pending_checkpoints_.clear();
                pending_batches_.clear();
            }
            queue_next_checkpoint();
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
        schedule_recovery_activation();
        queue_next_checkpoint();
        return true;
    }

    void ConsensusService::deactivate() {
        std::lock_guard lock(mutex_);
        connections_.clear();
        authenticator_.reset();
        intent_store_.reset();
        intent_pool_ = IntentPool {};
        committed_nonces_.clear();
        if (timeout_task_) {
            timeout_task_->cancel();
        }
        timeout_task_.reset();
        if (recovery_task_) {
            recovery_task_->cancel();
        }
        recovery_task_.reset();
        if (intent_batch_task_) {
            intent_batch_task_->cancel();
        }
        intent_batch_task_.reset();
        consensus_.reset();
        applied_checkpoint_.reset();
        latest_proposal_.reset();
        latest_certificate_.reset();
        latest_timeout_certificate_.reset();
        pending_checkpoints_.clear();
        pending_batches_.clear();
        pending_proposals_.clear();
        voting_enabled_ = false;
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
        case MessageType::ConsensusIntent: {
            const auto value = MessagePack::deserialize<IntentEnvelope>(serialized);
            if (status == MessageStatus::NoStatus && value.has_value()) {
                receive_intent(value.value());
            }
            break;
        }
        case MessageType::ConsensusBootstrapRequest: {
            if (serialized.size() > 64ULL * 1024ULL) {
                break;
            }
            const auto value = MessagePack::deserialize<ShadowBootstrapRequest>(serialized);
            if (status == MessageStatus::Request && value.has_value()) {
                receive_bootstrap_request(value.value(), responder, peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusBootstrapResponse: {
            if (serialized.size() > MaximumBootstrapBytes) {
                break;
            }
            const auto value = MessagePack::deserialize<ShadowBootstrapResponse>(serialized);
            if (status == MessageStatus::Response && value.has_value()) {
                bootstrap_event_.publish(value.value(), peer_identifier);
            }
            break;
        }
        case MessageType::ConsensusRecovery: {
            if (serialized.size() > 1ULL * 1024ULL * 1024ULL) {
                break;
            }
            const auto value = MessagePack::deserialize<RecoveryRequestV1>(serialized);
            if (status == MessageStatus::NoStatus && value.has_value()) {
                receive_recovery(value.value(), peer_identifier);
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
        } else {
            eWarning("[Consensus] Validator challenge could not be answered: {}",
                     std::to_underlying(response.error()));
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
        eLog("[Consensus] Authenticated validator {} on peer {}", verified.value(), peer_identifier);
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
            if (observed.error() == ConsensusError::InvalidNonce) {
                // A stale-nonce proposal means an authenticated proposer fell behind
                // and does not know it: it will re-propose the same doomed batch
                // forever. Hand it our verified tip in a focused reply so it can
                // catch up; no amplification, one certificate per bad proposal.
                const auto& state = consensus_->engine().safety_state();
                if (state.highest_certificate.has_value()
                    && state.highest_certificate.value().height >= proposal.header.height) {
                    send_to_peer(state.highest_certificate.value(),
                                 MessageType::ConsensusCertificate,
                                 std::string(peer_identifier),
                                 MessageStatus::NoStatus);
                }
            }
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
            eDebug("[Shadow] Serving batch {} to {}", request.header_hash.substr(0, 12), peer_identifier);
            responder.send_response(batch.value(),
                                    MessageType::ConsensusBatchData,
                                    SendMode::Focused,
                                    MessageStatus::Response);
        } else {
            eDebug("[Shadow] Batch {} requested by {} is not stored",
                   request.header_hash.substr(0, 12),
                   peer_identifier);
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
            eDebug("[Shadow] Dropped batch {} without a pending proposal", batch.header_hash.substr(0, 12));
            return;
        }
        eDebug("[Shadow] Resuming validation for batch {}", batch.header_hash.substr(0, 12));
        const auto ancestors = staged_ancestors_for(proposal->second);
        if (!ancestors.has_value()) {
            eWarning("[Shadow] Batch {} has an unusable ancestor chain: {}",
                     batch.header_hash,
                     std::to_underlying(ancestors.error()));
            return;
        }
        const auto valid = node_.dag()->validate_shadow_batch(proposal->second,
                                                              batch,
                                                              consensus_->configuration().maximum_batch_bytes,
                                                              ancestors.value());
        if (!valid.has_value()) {
            eWarning("[Shadow] Batch {} from {} failed validation with error {}",
                     batch.header_hash,
                     peer_identifier,
                     std::to_underlying(valid.error()));
            return;
        }
        const auto admitted = admit_batch_intents(proposal->second, batch);
        if (!admitted.has_value()) {
            eWarning("[Shadow] Batch {} intents could not be admitted: {}",
                     batch.header_hash,
                     std::to_underlying(admitted.error()));
            return;
        }
        const auto& highest           = consensus_->engine().safety_state().highest_certificate;
        const bool  already_certified = highest.has_value() && highest.value().header_hash == batch.header_hash;
        const auto  staged            = voting_enabled_ && !already_certified
                                            ? consensus_->engine().stage_batch_for_vote(batch)
                                            : consensus_->engine().stage_batch(batch);
        if (!staged.has_value()) {
            eWarning("[Shadow] Batch {} could not be staged", batch.header_hash);
            return;
        }
        if (already_certified) {
            pending_proposals_.erase(proposal);
            queue_next_checkpoint();
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
            const auto ancestors = staged_ancestors_for(proof.finalized_proposal);
            const auto valid =
                ancestors.has_value()
                    ? node_.dag()->validate_shadow_batch(proof.finalized_proposal,
                                                         batch->second,
                                                         consensus_->configuration().maximum_batch_bytes,
                                                         ancestors.value())
                    : std::unexpected(ancestors.error());
            if (!valid.has_value() || !admit_batch_intents(proof.finalized_proposal, batch->second).has_value()
                || !consensus_->engine().stage_batch(batch->second).has_value()) {
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
        const auto reconciled = reconcile_finalized_checkpoint();
        if (!reconciled.has_value()) {
            eCritical("[Shadow] Synchronized finality could not be applied: {}",
                      std::to_underlying(reconciled.error()));
            halt_voting();
            return;
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

    void ConsensusService::receive_bootstrap_request(const ShadowBootstrapRequest& request,
                                                     const Responder&              responder,
                                                     std::string_view              peer_identifier) {
        (void)peer_identifier;
        std::lock_guard lock(mutex_);
        if (!consensus_ || !consensus_->trust_anchor().has_value() || request.protocol_version != ProtocolVersion
            || request.network_id != consensus_->engine().validators().document().network_id
            || request.trust_anchor_hash != hash_trust_anchor(consensus_->trust_anchor().value())
            || request.maximum_entries == 0 || request.maximum_entries > MaximumBootstrapEntries) {
            return;
        }
        const auto             starts = consensus_->epoch_starts();
        BootstrapHistoryPageV1 page {
            .network_id        = request.network_id,
            .trust_anchor_hash = request.trust_anchor_hash,
            .after_epoch       = request.after_epoch,
        };
        const auto begin = std::ranges::find_if(starts, [&](const EpochStartV1& start) {
            return start.validators.epoch > request.after_epoch;
        });
        for (auto iterator = begin; iterator != starts.end() && page.entries.size() < request.maximum_entries;
             ++iterator) {
            page.entries.push_back(*iterator);
        }
        if (begin != starts.end()) {
            const auto remaining = static_cast<std::size_t>(std::distance(begin, starts.end()));
            if (remaining > page.entries.size() && !page.entries.empty()) {
                page.next_after_epoch = page.entries.back().validators.epoch;
            }
        }

        const auto  finalized_height = consensus_->engine().safety_state().finalized_height;
        std::string latest_header_hash;
        if (finalized_height != 0) {
            const auto proof = consensus_->engine().finality_proofs_after(finalized_height - 1, 1);
            if (proof.has_value() && proof.value().size() == 1) {
                latest_header_hash = hash_header(proof.value().front().finalized_proposal.header);
            }
        }
        ShadowBootstrapResponse response {
            .network_id                   = request.network_id,
            .page                         = std::move(page),
            .latest_epoch                 = consensus_->engine().validators().document().epoch,
            .latest_finalized_height      = finalized_height,
            .latest_finalized_header_hash = std::move(latest_header_hash),
        };
        while (!response.page.entries.empty() && MessagePack::serialize(response).size() > MaximumBootstrapBytes) {
            if (response.page.entries.size() == 1) {
                eWarning("[Shadow] Epoch {} exceeds the bootstrap response limit",
                         response.page.entries.front().validators.epoch);
                return;
            }
            response.page.entries.pop_back();
            response.page.next_after_epoch = response.page.entries.back().validators.epoch;
        }
        if (MessagePack::serialize(response).size() > MaximumBootstrapBytes) {
            return;
        }
        responder.send_response(response,
                                MessageType::ConsensusBootstrapResponse,
                                SendMode::Focused,
                                MessageStatus::Response);
    }

    void ConsensusService::receive_recovery(const RecoveryRequestV1& request, std::string_view peer_identifier) {
        (void)peer_identifier;
        std::lock_guard lock(mutex_);
        if (!consensus_ || request.protocol_version != ProtocolVersion) {
            return;
        }
        const auto scheduled =
            consensus_->schedule_recovery(request.recovery, request.next_validators, wall_clock_millis());
        if (!scheduled.has_value()) {
            if (scheduled.error() == ConsensusError::RecoveryConflict) {
                halt_voting();
                eCritical("[Shadow] Conflicting emergency decisions use the same recovery sequence");
            }
            return;
        }
        halt_voting();
        schedule_recovery_activation();
    }

    void ConsensusService::receive_intent(const IntentEnvelope& envelope) {
        std::lock_guard lock(mutex_);
        const auto      accepted = accept_intent(envelope, false);
        if (!accepted.has_value() && accepted.error() != ConsensusError::DuplicateIntent) {
            eWarning("[Shadow] Intent {} was rejected with error {}",
                     hash_intent(envelope.intent),
                     std::to_underlying(accepted.error()));
        }
    }

    std::expected<std::string, ConsensusError> ConsensusService::submit_intent(const IntentEnvelope& envelope) {
        std::lock_guard lock(mutex_);
        return accept_intent(envelope, true);
    }

    std::vector<IntentEnvelope> ConsensusService::ready_intents(std::size_t maximum_count,
                                                                std::size_t maximum_bytes) const {
        std::lock_guard lock(mutex_);
        return consensus_ ? intent_pool_.ready(committed_nonces_, intent_height(), maximum_count, maximum_bytes)
                          : std::vector<IntentEnvelope> {};
    }

    std::expected<std::optional<IntentReceipt>, ConsensusError> ConsensusService::intent_receipt(
        std::string_view intent_hash) {
        std::lock_guard lock(mutex_);
        return intent_store_ ? intent_store_->receipt(intent_hash)
                             : std::expected<std::optional<IntentReceipt>, ConsensusError>(
                                   std::unexpected(ConsensusError::NotReady));
    }

    std::expected<void, ConsensusError> ConsensusService::finalize_intents(
        const std::vector<std::pair<IntentEnvelope, IntentReceipt>>& finalized,
        std::optional<AppliedCheckpoint>                             checkpoint) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !intent_store_) {
            return std::unexpected(ConsensusError::NotReady);
        }
        auto ordered = finalized;
        std::ranges::sort(ordered, [](const auto& left, const auto& right) {
            return std::tie(left.first.intent.sender, left.first.intent.account_nonce)
                   < std::tie(right.first.intent.sender, right.first.intent.account_nonce);
        });
        auto                     next_nonces = committed_nonces_;
        std::vector<std::string> hashes;
        hashes.reserve(ordered.size());
        for (const auto& [envelope, receipt] : ordered) {
            const auto hash          = hash_intent(envelope.intent);
            const auto current_nonce = next_nonces[envelope.intent.sender];
            if (receipt.status != IntentStatus::Finalized || receipt.intent_hash != hash
                || envelope.intent.account_nonce > current_nonce + 1) {
                return std::unexpected(ConsensusError::InvalidNonce);
            }
            if (envelope.intent.account_nonce == current_nonce + 1) {
                next_nonces[envelope.intent.sender] = envelope.intent.account_nonce;
            }
            hashes.push_back(hash);
        }
        const auto committed = intent_store_->commit_finalized(ordered, checkpoint);
        if (!committed.has_value()) {
            return std::unexpected(committed.error());
        }
        committed_nonces_ = std::move(next_nonces);
        if (checkpoint.has_value()) {
            applied_checkpoint_ = std::move(checkpoint);
        }
        intent_pool_.erase(hashes);
        return {};
    }

    std::expected<void, ConsensusError> ConsensusService::submit_recovery(const RecoveryDocumentV2& recovery,
                                                                          ValidatorSet  next_validators,
                                                                          std::uint64_t now_ms) {
        std::lock_guard lock(mutex_);
        if (!consensus_) {
            return std::unexpected(ConsensusError::NotReady);
        }
        const auto request = RecoveryRequestV1 {
            .recovery        = recovery,
            .next_validators = next_validators,
        };
        const auto scheduled = consensus_->schedule_recovery(recovery, std::move(next_validators), now_ms);
        if (!scheduled.has_value()) {
            if (scheduled.error() == ConsensusError::RecoveryConflict) {
                halt_voting();
            }
            return std::unexpected(scheduled.error());
        }
        halt_voting();
        send_to_validators(request, MessageType::ConsensusRecovery);
        schedule_recovery_activation();
        return {};
    }

    std::expected<std::size_t, ConsensusError> ConsensusService::request_bootstrap_history(
        const TrustAnchorV1& anchor,
        std::uint64_t        after_epoch) {
        std::lock_guard lock(mutex_);
        if (!verify_trust_anchor(anchor) || after_epoch < anchor.initial_validators.epoch) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        const auto peers = node_.network()->active_full_peers_with_capability(SHADOW_CONSENSUS_CAPABILITY);
        constexpr std::size_t RequiredBootstrapPeers = 3;
        if (peers.size() < RequiredBootstrapPeers) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        const ShadowBootstrapRequest request {
            .network_id        = anchor.network_id,
            .trust_anchor_hash = hash_trust_anchor(anchor),
            .after_epoch       = after_epoch,
            .maximum_entries   = static_cast<std::uint16_t>(MaximumBootstrapEntries),
        };
        for (std::size_t index = 0; index < RequiredBootstrapPeers; ++index) {
            send_to_peer(request, MessageType::ConsensusBootstrapRequest, peers[index], MessageStatus::Request);
        }
        return RequiredBootstrapPeers;
    }

    std::expected<bool, ConsensusError> ConsensusService::activate_pending_recovery(std::uint64_t now_ms) {
        if (!consensus_) {
            return std::unexpected(ConsensusError::NotReady);
        }
        const auto activated = consensus_->activate_scheduled_recovery(now_ms);
        if (!activated.has_value() || !activated.value()) {
            if (activated.has_value()) {
                schedule_recovery_activation();
            }
            return activated;
        }
        authenticator_ = std::make_unique<PeerAuthenticator>(consensus_->engine().validators(),
                                                             consensus_->engine().identity());
        latest_proposal_.reset();
        latest_certificate_.reset();
        latest_timeout_certificate_.reset();
        pending_checkpoints_.clear();
        pending_batches_.clear();
        pending_proposals_.clear();
        voting_enabled_ = consensus_->engine().identity().has_value();
        reset_timeout();
        queue_next_checkpoint();
        return true;
    }

    void ConsensusService::schedule_recovery_activation() {
        if (!consensus_ || !recovery_task_ || !consensus_->pending_recovery().has_value()) {
            return;
        }
        const auto& pending = consensus_->pending_recovery().value();
        const auto  target =
            std::max(pending.document.activate_after_ms, pending.first_seen_ms + MinimumRecoveryDelayMillis);
        const auto now   = wall_clock_millis();
        const auto delay = target > now ? target - now : 0;
        recovery_task_->schedule_earlier(std::chrono::milliseconds(delay));
    }

    std::expected<std::string, ConsensusError> ConsensusService::accept_intent(const IntentEnvelope& envelope,
                                                                               bool                  broadcast) {
        if (!consensus_ || !intent_store_
            || envelope.intent.network_id != consensus_->engine().validators().document().network_id) {
            return std::unexpected(consensus_ ? ConsensusError::InvalidNetwork : ConsensusError::NotReady);
        }
        const auto actor = node_.actor_index()->read_actor(envelope.intent.sender, ActorGetType::NoRequest);
        if (!actor.has_value()) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        const auto hash     = hash_intent(envelope.intent);
        const auto accepted = intent_pool_.submit(envelope,
                                                  Utils::to_base64(actor.value().key().public_key()),
                                                  committed_nonces_[envelope.intent.sender],
                                                  intent_height());
        if (!accepted.has_value()) {
            return accepted.error() == ConsensusError::DuplicateIntent
                       ? std::expected<std::string, ConsensusError>(hash)
                       : std::unexpected(accepted.error());
        }
        const auto stored = intent_store_->put(envelope);
        if (!stored.has_value()) {
            intent_pool_.erase({ hash });
            return std::unexpected(stored.error());
        }
        if (broadcast) {
            node_.network()->send_message(envelope,
                                          MessageType::ConsensusIntent,
                                          SendMode::Broadcast,
                                          MessageStatus::NoStatus);
        }
        if (intent_batch_task_) {
            intent_batch_task_->schedule_earlier(std::chrono::milliseconds(25));
        } else {
            queue_next_checkpoint();
        }
        return hash;
    }

    std::uint64_t ConsensusService::intent_height() const noexcept {
        if (!consensus_) {
            return 0;
        }
        const auto& certificate = consensus_->engine().safety_state().highest_certificate;
        return certificate.has_value() ? certificate.value().height + 1 : 1;
    }

    bool ConsensusService::apply_certificate(const QuorumCertificate& certificate) {
        const auto finalized = consensus_->receive_certificate(certificate);
        if (!finalized.has_value()) {
            eWarning("[Consensus] Certificate {} at height {} was rejected with error {}",
                     hash_certificate(certificate),
                     certificate.height,
                     std::to_underlying(finalized.error()));
            if (finalized.error() == ConsensusError::DataUnavailable) {
                send_to_validators(
                    ShadowSyncRequest {
                        .protocol_version = ProtocolVersion,
                        .network_id       = consensus_->engine().validators().document().network_id,
                        .epoch            = consensus_->engine().validators().document().epoch,
                        .finalized_height = consensus_->engine().safety_state().finalized_height,
                    },
                    MessageType::ConsensusSyncRequest,
                    MessageStatus::Request);
            }
            return false;
        }
        latest_certificate_ = certificate;
        if (latest_proposal_.has_value()
            && certificate.header_hash == hash_header(latest_proposal_.value().header)) {
            pending_batches_.erase(latest_proposal_.value().batch.last_section);
            pending_checkpoints_.erase(latest_proposal_.value().batch.last_section);
        }
        reset_timeout();
        if (finalized.value().has_value()) {
            const auto& checkpoint = finalized.value().value();
            if (consensus_->configuration().mode == ShadowMode::Finality) {
                const auto reconciled = reconcile_finalized_checkpoint();
                if (!reconciled.has_value()) {
                    eCritical("[Shadow] Finalized checkpoint {} could not be applied: {}",
                              checkpoint.header_hash,
                              std::to_underlying(reconciled.error()));
                    halt_voting();
                    return false;
                }
            } else {
                finalized_event_.publish(checkpoint);
                eInfo("[Shadow] Finalized height {} at Dag section {}", checkpoint.height, checkpoint.dag_section);
            }
        }
        queue_next_checkpoint();
        return true;
    }

    std::expected<void, ConsensusError> ConsensusService::apply_finality_proof(const FinalityProof& proof) {
        const auto& proposal   = proof.finalized_proposal;
        const auto  checkpoint = AppliedCheckpoint {
             .height      = proposal.header.height,
             .header_hash = hash_header(proposal.header),
        };
        const auto activation_height = consensus_->engine().epoch_bootstrap().has_value()
                                           ? consensus_->engine().epoch_bootstrap().value().activation_height
                                           : consensus_->configuration().activation_height;
        const auto first_height      = std::max<std::uint64_t>(1, activation_height);
        if (applied_checkpoint_.has_value()
            && applied_checkpoint_.value().height == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ConsensusError::InvalidHeight);
        }
        const auto expected_height =
            applied_checkpoint_.has_value() ? applied_checkpoint_.value().height + 1 : first_height;
        if (checkpoint.height != expected_height
            || proposal.header.dag_section < consensus_->configuration().activation_dag_section) {
            return std::unexpected(ConsensusError::InvalidHeight);
        }
        auto batch = consensus_->engine().batch_for(checkpoint.header_hash);
        if (!batch.has_value()) {
            auto rebuilt = node_.dag()->build_shadow_batch(SectionId(proposal.batch.first_section),
                                                           SectionId(proposal.batch.last_section),
                                                           checkpoint.header_hash);
            if (!rebuilt.has_value()
                || hash_batch_manifest(rebuilt.value().manifest) != proposal.header.batch_root) {
                return std::unexpected(ConsensusError::DataUnavailable);
            }
            batch = std::move(rebuilt.value());
        }
        const auto installed = node_.dag()->install_shadow_batch(proposal,
                                                                 batch.value(),
                                                                 consensus_->configuration().maximum_batch_bytes,
                                                                 hash_certificate(proof.decision_certificate));
        if (!installed.has_value()) {
            return std::unexpected(installed.error());
        }
        const auto finalized = finalized_intents(proposal, batch.value());
        if (!finalized.has_value()) {
            return std::unexpected(finalized.error());
        }
        const auto epoch_changes = process_epoch_changes(finalized.value());
        if (!epoch_changes.has_value()) {
            return std::unexpected(epoch_changes.error());
        }
        const auto intents_committed = finalize_intents(finalized.value(), checkpoint);
        if (!intents_committed.has_value()) {
            return std::unexpected(intents_committed.error());
        }
        const auto epoch_activated = activate_pending_epoch();
        if (!epoch_activated.has_value()) {
            return std::unexpected(epoch_activated.error());
        }
        const auto finalized_checkpoint = FinalizedCheckpoint {
            .height            = proposal.header.height,
            .dag_section       = proposal.header.dag_section,
            .first_dag_section = proposal.batch.first_section,
            .header_hash       = checkpoint.header_hash,
            .section_root      = proposal.header.section_root,
            .transaction_root  = proposal.header.transaction_root,
            .batch_root        = proposal.header.batch_root,
            .certificate_hash  = hash_certificate(proof.decision_certificate),
        };
        finalized_event_.publish(finalized_checkpoint);
        eInfo("[Shadow] Finalized height {} at Dag section {}",
              finalized_checkpoint.height,
              finalized_checkpoint.dag_section);
        return {};
    }

    std::expected<void, ConsensusError> ConsensusService::reconcile_finalized_checkpoint() {
        if (!consensus_ || consensus_->configuration().mode != ShadowMode::Finality) {
            return {};
        }
        const auto finalized_height  = consensus_->engine().safety_state().finalized_height;
        const auto activation_height = consensus_->engine().epoch_bootstrap().has_value()
                                           ? consensus_->engine().epoch_bootstrap().value().activation_height
                                           : consensus_->configuration().activation_height;
        if (finalized_height < activation_height || finalized_height == 0) {
            return {};
        }
        if (applied_checkpoint_.has_value() && applied_checkpoint_.value().height > finalized_height) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        const auto first_height = std::max<std::uint64_t>(1, activation_height);
        auto cursor = applied_checkpoint_.has_value() ? applied_checkpoint_.value().height : first_height - 1;
        while (cursor < finalized_height) {
            const auto proofs = consensus_->engine().finality_proofs_after(cursor, MaximumShadowSyncProofs);
            if (!proofs.has_value() || proofs.value().empty()) {
                return std::unexpected(proofs.has_value() ? ConsensusError::StorageFailure : proofs.error());
            }
            for (const auto& proof : proofs.value()) {
                const auto height = proof.finalized_proposal.header.height;
                if (height > finalized_height) {
                    break;
                }
                if (height != cursor + 1) {
                    return std::unexpected(ConsensusError::InvalidHeight);
                }
                const auto applied = apply_finality_proof(proof);
                if (!applied.has_value()) {
                    return std::unexpected(applied.error());
                }
                cursor = height;
            }
        }
        return {};
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
        return consensus_ && voting_enabled_ && consensus_->engine().identity().has_value();
    }

    bool ConsensusService::controls_section(std::uint64_t section) const {
        std::lock_guard lock(mutex_);
        return consensus_ && consensus_->configuration().mode == ShadowMode::Finality
               && section >= consensus_->configuration().activation_dag_section;
    }

    bool ConsensusService::requires_intent_v2() const noexcept {
        std::lock_guard lock(mutex_);
        if (!consensus_ || consensus_->configuration().mode != ShadowMode::Finality
            || !consensus_->engine().safety_state().highest_certificate.has_value()) {
            return false;
        }
        const auto height = consensus_->engine().safety_state().highest_certificate.value().height;
        return height != std::numeric_limits<std::uint64_t>::max()
               && height + 1 >= consensus_->configuration().activation_height;
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
                                                      consensus_->configuration().maximum_batch_bytes,
                                                      hash_certificate(proof_value.decision_certificate));
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

    std::expected<std::optional<TransactionInclusionProofV1>, ConsensusError> ConsensusService::
        transaction_inclusion_proof(std::string_view transaction_hash) const {
        std::lock_guard lock(mutex_);
        if (!consensus_) {
            return std::unexpected(ConsensusError::NotReady);
        }
        return consensus_->engine().transaction_inclusion_proof(transaction_hash);
    }

    bool ConsensusService::verify_transaction_inclusion_proof(const TransactionInclusionProofV1& proof) const {
        std::lock_guard lock(mutex_);
        return consensus_ && consensus_->engine().verify_transaction_inclusion_proof(proof);
    }

    ConsensusMetricsSnapshot ConsensusService::metrics() const noexcept {
        std::lock_guard lock(mutex_);
        return consensus_ ? consensus_->engine().metrics() : ConsensusMetricsSnapshot {};
    }

    Core::Event<const FinalizedCheckpoint&>& ConsensusService::finalized_event() noexcept {
        return finalized_event_;
    }

    Core::Event<const ShadowBootstrapResponse&, std::string_view>& ConsensusService::bootstrap_event() noexcept {
        return bootstrap_event_;
    }

    void ConsensusService::peer_connected(const std::string& identifier) {
        std::lock_guard lock(mutex_);
        if (!authenticator_) {
            return;
        }
        challenge_peer(identifier, true);
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

    void ConsensusService::challenge_peer(const std::string& identifier, bool reset_existing) {
        if (!authenticator_) {
            return;
        }
        if (reset_existing) {
            authenticator_->forget_peer(identifier);
        } else if (authenticator_->authenticated_validator(identifier).has_value()) {
            return;
        }
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
        } else {
            eWarning("[Consensus] Validator challenge could not be created for peer {}: {}",
                     identifier,
                     std::to_underlying(challenge.error()));
        }
    }

    void ConsensusService::refresh_peer_authentication() {
        if (!authenticator_) {
            return;
        }
        for (const auto& identifier :
             node_.network()->active_full_peers_with_capability(SHADOW_CONSENSUS_CAPABILITY)) {
            challenge_peer(identifier, false);
        }
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
        const auto& highest = consensus_->engine().safety_state().highest_certificate;
        if (!highest.has_value()) {
            return;
        }
        const auto state = build_state_commitment(batch.value(),
                                                  control.value().control,
                                                  highest.value().height + 1,
                                                  highest.value());
        if (!state.has_value()) {
            eWarning("[Shadow] Cannot build state commitment for section {}: {}",
                     section,
                     std::to_underlying(state.error()));
            return;
        }
        pending_checkpoints_.insert_or_assign(section,
                                              ShadowCheckpoint {
                                                  .batch = batch.value().manifest,
                                                  .state = state.value(),
                                              });
        pending_batches_.insert_or_assign(section, batch.value());
        propose_checkpoint(consensus_->engine().safety_state().current_round);
    }

    void ConsensusService::queue_next_checkpoint() {
        if (!consensus_ || !consensus_->engine().safety_state().highest_certificate.has_value()) {
            return;
        }
        const auto&             highest = consensus_->engine().safety_state().highest_certificate.value();
        auto                    target  = consensus_->configuration().activation_dag_section;
        std::optional<Proposal> highest_proposal;
        if (highest.phase == Phase::Genesis) {
            if (consensus_->engine().epoch_bootstrap().has_value()) {
                const auto first = consensus_->engine().epoch_bootstrap().value().first_dag_section;
                if (first > std::numeric_limits<std::uint64_t>::max() - (ShadowSectionInterval - 1)) {
                    return;
                }
                target = first + ShadowSectionInterval - 1;
            } else {
                target = target == 0 ? ShadowSectionInterval : target;
            }
        } else {
            highest_proposal = consensus_->engine().proposal_for(highest.header_hash);
            if (!highest_proposal.has_value()) {
                // A certificate can arrive before its proposal, so recovery must use authenticated sync.
                eWarning("[Shadow] Proposal for certified height {} is not stored yet; requesting sync",
                         highest.height);
                send_to_validators(
                    ShadowSyncRequest {
                        .protocol_version = ProtocolVersion,
                        .network_id       = consensus_->engine().validators().document().network_id,
                        .epoch            = consensus_->engine().validators().document().epoch,
                        .finalized_height = consensus_->engine().safety_state().finalized_height,
                    },
                    MessageType::ConsensusSyncRequest,
                    MessageStatus::Request);
                return;
            }
            if (highest_proposal.value().batch.last_section
                > std::numeric_limits<std::uint64_t>::max() - ShadowSectionInterval) {
                return;
            }
            target = highest_proposal.value().batch.last_section + ShadowSectionInterval;
        }
        if (requires_intent_v2()) {
            if (pending_checkpoints_.contains(target)) {
                propose_checkpoint(consensus_->engine().safety_state().current_round);
                return;
            }
            const auto flush_pipeline = has_unfinalized_intents();
            const auto intents        = flush_pipeline
                                            ? std::vector<IntentEnvelope> {}
                                            : intent_pool_.ready(committed_nonces_,
                                                          intent_height(),
                                                          20 * 256,
                                                          consensus_->configuration().maximum_batch_bytes / 2);
            if (intents.empty() && !flush_pipeline) {
                return;
            }
            const auto first = target == 0 ? SectionId(0) : SectionId(target) - CONTROL_INTERVAL_DIFF;
            std::optional<std::string> previous_section_bytes;
            std::string                previous_section_root;
            if (highest.phase != Phase::Genesis) {
                const auto& parent       = highest_proposal.value();
                const auto  parent_batch = consensus_->engine().batch_for(highest.header_hash);
                if (!parent_batch.has_value()) {
                    // A certified proposal can arrive before its batch payload.
                    eWarning("[Shadow] Batch for certified height {} is not stored yet; requesting it",
                             highest.height);
                    pending_proposals_.insert_or_assign(highest.header_hash, parent);
                    send_to_validators(
                        SectionBatchRequest {
                            .protocol_version = ProtocolVersion,
                            .network_id       = consensus_->engine().validators().document().network_id,
                            .epoch            = consensus_->engine().validators().document().epoch,
                            .header_hash      = highest.header_hash,
                        },
                        MessageType::ConsensusBatchRequest,
                        MessageStatus::Request);
                    return;
                }
                if (parent_batch.value().sections.empty()
                    || parent_batch.value().sections.back().first != parent.batch.last_section) {
                    eWarning("[Shadow] Voting halted: parent batch for height {} is inconsistent", highest.height);
                    halt_voting();
                    return;
                }
                previous_section_bytes = parent_batch.value().sections.back().second;
                previous_section_root  = parent.header.section_root;
            }
            auto batch = node_.dag()->build_shadow_intent_batch(first,
                                                                SectionId(target),
                                                                highest.height + 1,
                                                                intents,
                                                                {},
                                                                std::move(previous_section_bytes),
                                                                std::move(previous_section_root));
            if (!batch.has_value()) {
                eWarning("[Shadow] Leader could not materialize the next intent batch: {}",
                         std::to_underlying(batch.error()));
                return;
            }
            if (batch.value().manifest.payload_bytes > consensus_->configuration().maximum_batch_bytes) {
                eWarning("[Shadow] Leader intent batch exceeds the configured byte limit");
                return;
            }
            const auto section_root = node_.dag()->shadow_batch_section_root(batch.value());
            if (!section_root.has_value()) {
                eWarning("[Shadow] Leader could not calculate the intent batch root");
                return;
            }
            const auto state =
                build_state_commitment(batch.value(), section_root.value(), highest.height + 1, highest);
            if (!state.has_value()) {
                eWarning("[Shadow] Leader could not calculate the state commitment: {}",
                         std::to_underlying(state.error()));
                return;
            }
            pending_checkpoints_.insert_or_assign(target,
                                                  ShadowCheckpoint {
                                                      .batch = batch.value().manifest,
                                                      .state = state.value(),
                                                  });
            pending_batches_.insert_or_assign(target, std::move(batch.value()));
            propose_checkpoint(consensus_->engine().safety_state().current_round);
            return;
        }
        if (node_.dag()->read_control(SectionId(target)).has_value()) {
            checkpoint_ready(target);
        }
    }

    void ConsensusService::propose_checkpoint(std::uint64_t round) {
        if (!consensus_ || !voting_enabled_ || pending_checkpoints_.empty()) {
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
            pending_batches_.erase(pending_checkpoints_.begin()->first);
            pending_checkpoints_.erase(pending_checkpoints_.begin());
        }
        if (pending_checkpoints_.empty()) {
            return;
        }
        if (next_section != 0 && pending_checkpoints_.begin()->second.batch.first_section != next_section) {
            eWarning("[Shadow] Pending checkpoint {}..{} does not start at expected section {}; not proposing",
                     pending_checkpoints_.begin()->second.batch.first_section,
                     pending_checkpoints_.begin()->second.batch.last_section,
                     next_section);
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
        const auto  stored_batch   = pending_batches_.find(proposal_value.batch.last_section);
        if (stored_batch == pending_batches_.end()) {
            eWarning("[Shadow] Checkpoint for section {} has no stored batch; not proposing",
                     proposal_value.batch.last_section);
            return;
        }
        auto batch        = stored_batch->second;
        batch.header_hash = hash_header(proposal_value.header);
        if (hash_batch_manifest(batch.manifest) != proposal_value.header.batch_root) {
            eWarning("[Shadow] Stored batch for section {} does not match the checkpoint root; not proposing",
                     proposal_value.batch.last_section);
            return;
        }
        const auto valid = [&]() -> std::expected<void, ConsensusError> {
            try {
                const auto ancestors = staged_ancestors_for(proposal_value);
                if (!ancestors.has_value()) {
                    return std::unexpected(ancestors.error());
                }
                return node_.dag()->validate_shadow_batch(proposal_value,
                                                          batch,
                                                          consensus_->configuration().maximum_batch_bytes,
                                                          ancestors.value());
            } catch (const std::exception& exception) {
                eCritical("[Shadow] Leader batch validation failed with an exception: {}", exception.what());
                return std::unexpected(ConsensusError::StorageFailure);
            }
        }();
        if (!valid.has_value()) {
            eWarning("[Shadow] Leader batch validation failed: {}", std::to_underlying(valid.error()));
            if (valid.error() == ConsensusError::InvalidRoot) {
                evict_unprovable_intents(proposal_value, batch);
            }
            return;
        }
        const auto admitted = admit_batch_intents(proposal_value, batch);
        if (!admitted.has_value()) {
            eWarning("[Shadow] Leader intent admission failed: {}", std::to_underlying(admitted.error()));
            return;
        }
        const auto staged = consensus_->engine().stage_batch_for_vote(batch);
        if (!staged.has_value()) {
            eWarning("[Shadow] Leader could not stage the proposed batch: {}", std::to_underlying(staged.error()));
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
        if (!voting_enabled_) {
            return;
        }
        const auto vote = consensus_->engine().accept_proposal(proposal);
        if (!vote.has_value()) {
            const auto batch = consensus_->engine().batch_for(hash_header(proposal.header));
            if (batch.has_value() && !consensus_->engine().stage_batch(batch.value()).has_value()) {
                eCritical("[Shadow] Rejected proposal batch could not be persisted");
                halt_voting();
            }
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
        if (!consensus_ || !voting_enabled_ || !consensus_->engine().identity().has_value()
            || !consensus_->engine().safety_state().highest_certificate.has_value()) {
            return;
        }
        refresh_peer_authentication();
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
        queue_next_checkpoint();
        reset_timeout();
    }

    void ConsensusService::reset_timeout() {
        if (!timeout_task_ || !consensus_ || !voting_enabled_) {
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

    void ConsensusService::halt_voting() {
        voting_enabled_ = false;
        if (timeout_task_) {
            timeout_task_->cancel();
        }
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

    std::expected<std::vector<Transaction>, ConsensusError> ConsensusService::staged_ancestor_transactions(
        const QuorumCertificate& parent,
        std::uint64_t            first_section) const {
        if (!consensus_) {
            return std::unexpected(ConsensusError::NotReady);
        }

        std::vector<SectionBatchData> ancestors;
        std::set<std::string>         seen;
        auto                          certificate = parent;
        auto                          frontier    = first_section;
        const auto                    finalized   = consensus_->engine().safety_state().finalized_height;

        while (certificate.phase != Phase::Genesis && certificate.height > finalized) {
            if (certificate.header_hash.empty() || !seen.insert(certificate.header_hash).second
                || ancestors.size() == MaximumStagedAncestors) {
                return std::unexpected(ConsensusError::InvalidParent);
            }

            const auto staged   = consensus_->engine().batch_for(certificate.header_hash);
            const auto proposal = consensus_->engine().proposal_for(certificate.header_hash);
            if (!staged.has_value() || !proposal.has_value()
                || hash_header(proposal.value().header) != certificate.header_hash
                || proposal.value().header.height != certificate.height
                || staged.value().header_hash != certificate.header_hash
                || hash_batch_manifest(staged.value().manifest) != proposal.value().header.batch_root
                || staged.value().manifest.first_section > staged.value().manifest.last_section
                || staged.value().manifest.last_section == std::numeric_limits<std::uint64_t>::max()
                || staged.value().manifest.last_section + 1 != frontier) {
                return std::unexpected(ConsensusError::InvalidParent);
            }

            const auto& manifest = staged.value().manifest;
            if (manifest.payload_bytes > consensus_->configuration().maximum_batch_bytes
                || staged.value().sections.size() != manifest.last_section - manifest.first_section + 1
                || calculate_data_root(staged.value().sections) != manifest.data_root) {
                return std::unexpected(ConsensusError::InvalidParent);
            }
            std::uint64_t            expected_section = manifest.first_section;
            std::uint64_t            payload_bytes    = 0;
            std::vector<std::string> transaction_hashes;
            WireFormat::Scope        canonical_scope(WireFormat::Mode::Canonical);
            for (const auto& [section_value, bytes] : staged.value().sections) {
                if (section_value != expected_section
                    || bytes.size() > consensus_->configuration().maximum_batch_bytes - payload_bytes) {
                    return std::unexpected(ConsensusError::InvalidParent);
                }
                payload_bytes += bytes.size();
                auto section = Json::deserialize<Section>(bytes);
                if (!section.has_value()) {
                    return std::unexpected(ConsensusError::InvalidParent);
                }
                const auto section_id = SectionId(section_value);
                section.value().id    = section_id;
                section.value().control.reset();
                if (Json::serialize(section.value()) != bytes
                    || std::ranges::any_of(section.value().transactions, [&](const auto& transaction) {
                           return transaction.section() != section_id;
                       })) {
                    return std::unexpected(ConsensusError::InvalidParent);
                }
                for (const auto& transaction : section.value().transactions) {
                    transaction_hashes.push_back(consensus_transaction_hash(transaction));
                }
                ++expected_section;
            }
            if (payload_bytes != manifest.payload_bytes || transaction_hashes != manifest.transaction_hashes
                || calculate_transaction_root(transaction_hashes) != manifest.transaction_root) {
                return std::unexpected(ConsensusError::InvalidParent);
            }

            ancestors.push_back(staged.value());
            frontier    = staged.value().manifest.first_section;
            certificate = proposal.value().parent_certificate;
        }

        std::vector<Transaction> transactions;
        for (const auto& ancestor : std::ranges::reverse_view(ancestors)) {
            for (const auto& [section, bytes] : ancestor.sections) {
                const auto decoded = Json::deserialize<Section>(bytes);
                if (!decoded.has_value()) {
                    return std::unexpected(ConsensusError::InvalidParent);
                }
                for (const auto& transaction : decoded.value().transactions) {
                    transactions.push_back(transaction);
                }
            }
        }
        return transactions;
    }

    std::expected<std::set<Transaction>, ConsensusError> ConsensusService::staged_ancestors_for(
        const Proposal& proposal) const {
        if (!consensus_) {
            return std::unexpected(ConsensusError::NotReady);
        }
        const auto ancestry =
            staged_ancestor_transactions(proposal.parent_certificate, proposal.batch.first_section);
        if (!ancestry.has_value()) {
            return std::unexpected(ancestry.error());
        }
        return std::set<Transaction> { ancestry.value().begin(), ancestry.value().end() };
    }

    std::expected<StateCommitmentV2, ConsensusError> ConsensusService::build_state_commitment(
        const SectionBatchData&  batch,
        std::string_view         section_root,
        std::uint64_t            height,
        const QuorumCertificate& parent) const {
        if (!consensus_ || batch.sections.empty() || section_root.empty()
            || batch.manifest.first_section > batch.manifest.last_section) {
            return std::unexpected(ConsensusError::InvalidRoot);
        }

        const auto ancestry = staged_ancestor_transactions(parent, batch.manifest.first_section);
        if (!ancestry.has_value()) {
            return std::unexpected(ancestry.error());
        }

        std::vector<Transaction> transactions;
        std::vector<ActorId>     actors = node_.actor_index()->read_all_actors_ids();
        for (const auto& transaction : ancestry.value()) {
            if (!transaction.sender().is_zero()) {
                actors.push_back(transaction.sender());
            }
            if (!transaction.receiver().is_zero()) {
                actors.push_back(transaction.receiver());
            }
        }
        for (const auto& [section, bytes] : batch.sections) {
            const auto decoded = Json::deserialize<Section>(bytes);
            if (!decoded.has_value()) {
                return std::unexpected(ConsensusError::InvalidRoot);
            }
            for (const auto& transaction : decoded.value().transactions) {
                transactions.push_back(transaction);
                if (!transaction.sender().is_zero()) {
                    actors.push_back(transaction.sender());
                }
                if (!transaction.receiver().is_zero()) {
                    actors.push_back(transaction.receiver());
                }
            }
            if (section < batch.manifest.first_section || section > batch.manifest.last_section) {
                return std::unexpected(ConsensusError::InvalidRoot);
            }
        }
        std::ranges::sort(actors, {}, [](const ActorId& actor) {
            return actor.to_string();
        });
        actors.erase(std::unique(actors.begin(), actors.end()), actors.end());

        Balances balances;
        if (batch.manifest.first_section != 0) {
            balances = node_.dag()->calculate_actors_balance(actors, SectionId(batch.manifest.first_section - 1));
        }
        // Replay the certified-but-unfinalized ancestors first: their spends are not
        // in the canonical balances above, and our own batch is only valid relative
        // to the state they produced.
        for (const auto& transaction : ancestry.value()) {
            node_.dag()->cache().process_transaction(transaction, balances);
        }
        for (const auto& transaction : transactions) {
            node_.dag()->cache().process_transaction(transaction, balances);
        }
        std::map<std::string, std::string> account_state;
        for (const auto& [key, balance] : balances) {
            if (balance == 0) {
                continue;
            }
            account_state.emplace(key.first.to_string() + ':' + key.second.to_string(), balance.to_string());
        }

        std::map<std::string, std::string>           contract_state;
        std::map<std::string, std::string>           contract_owners;
        ExtraChain::Contracts::ContractCatalogFilter filter { .limit = 100 };
        do {
            const auto page = node_.dag()->cache().list_contracts(filter);
            for (const auto& summary : page.items) {
                if (summary.section < batch.manifest.first_section) {
                    contract_state.insert_or_assign(summary.contract_id, contract_state_value(summary));
                    contract_owners.insert_or_assign(summary.contract_id, summary.owner_id);
                }
            }
            filter.cursor = page.next_cursor;
        } while (filter.cursor.has_value());

        std::map<std::string, std::string> token_state;
        for (const auto& token : node_.token_manager()->read_registry()) {
            const auto section = token.section_id.has_value() ? token.section_id.value().to_int().value_or(0) : 0;
            if (section < batch.manifest.first_section) {
                token_state.insert_or_assign(token.token_id.to_string(), token_registry_state_value(token));
            }
        }

        for (const auto& transaction : transactions) {
            if (!is_contract_transaction(transaction.type()) || !transaction.meta().has_value()) {
                continue;
            }
            const auto metadata = Json::deserialize<ContractTransactionData>(transaction.meta().value());
            const auto section  = transaction.section().to_int();
            if (!metadata.has_value() || !section.has_value() || metadata.value().schema != 4) {
                return std::unexpected(ConsensusError::InvalidRoot);
            }
            const auto contract_id = transaction.receiver().to_string();
            auto       owner_id    = transaction.sender().to_string();
            if (transaction.type() != TransactionType::ContractDeploy) {
                const auto owner = contract_owners.find(contract_id);
                if (owner == contract_owners.end()) {
                    return std::unexpected(ConsensusError::InvalidRoot);
                }
                owner_id = owner->second;
            }
            contract_state.insert_or_assign(contract_id,
                                            contract_state_value(owner_id,
                                                                 metadata.value(),
                                                                 transaction.hash(),
                                                                 static_cast<std::uint64_t>(section.value())));
            contract_owners.insert_or_assign(contract_id, owner_id);
            if (ExtraChain::Contracts::is_system_token_kind(metadata.value().kind)
                && (transaction.type() == TransactionType::ContractDeploy
                    || (transaction.type() == TransactionType::ContractCall
                        && metadata.value().method == "import_legacy"
                        && metadata.value().legacy_migration.has_value()))) {
                const auto token_id = metadata.value().legacy_migration.has_value()
                                          ? metadata.value().legacy_migration.value().legacy_token_id
                                          : contract_id;
                token_state.insert_or_assign(token_id,
                                             token_registry_state_value(transaction.sender().to_string(),
                                                                        contract_id,
                                                                        metadata.value().kind,
                                                                        metadata.value().language,
                                                                        transaction.hash(),
                                                                        static_cast<std::uint64_t>(
                                                                            section.value())));
            }
            for (const auto& transition : metadata.value().transitions) {
                const auto owner = contract_owners.find(transition.contract_id);
                if (owner == contract_owners.end()) {
                    return std::unexpected(ConsensusError::InvalidRoot);
                }
                contract_state.insert_or_assign(transition.contract_id,
                                                contract_state_value(owner->second,
                                                                     transition,
                                                                     transaction.hash(),
                                                                     static_cast<std::uint64_t>(section.value())));
            }
        }

        std::string previous_state_commitment;
        if (parent.phase == Phase::Genesis) {
            if (consensus_->engine().epoch_bootstrap().has_value()) {
                previous_state_commitment =
                    consensus_->engine().epoch_bootstrap().value().previous_state_commitment;
            }
        } else {
            const auto parent_proposal = consensus_->engine().proposal_for(parent.header_hash);
            if (!parent_proposal.has_value()) {
                return std::unexpected(ConsensusError::InvalidParent);
            }
            previous_state_commitment = parent_proposal.value().header.state_commitment;
        }

        return StateCommitmentV2 {
            .protocol_version          = ProtocolVersion,
            .network_id                = consensus_->engine().validators().document().network_id,
            .epoch                     = consensus_->engine().validators().document().epoch,
            .height                    = height,
            .previous_state_commitment = std::move(previous_state_commitment),
            .section_root              = std::string(section_root),
            .account_state_root        = segmented_state_root("accounts", state_entries(account_state)),
            .contract_state_root       = segmented_state_root("contracts", state_entries(contract_state)),
            .token_registry_root       = segmented_state_root("tokens", state_entries(token_state)),
            .validator_set_hash        = consensus_->engine().validators().hash(),
        };
    }

    std::expected<void, ConsensusError> ConsensusService::validate_proposal(const Proposal& proposal) {
        if (!consensus_ || proposal.batch.payload_bytes > consensus_->configuration().maximum_batch_bytes) {
            return std::unexpected(ConsensusError::DataTooLarge);
        }
        const auto stored = consensus_->engine().batch_for(hash_header(proposal.header));
        if (stored.has_value()) {
            const auto ancestors = staged_ancestors_for(proposal);
            if (!ancestors.has_value()) {
                return std::unexpected(ancestors.error());
            }
            const auto valid = node_.dag()->validate_shadow_batch(proposal,
                                                                  stored.value(),
                                                                  consensus_->configuration().maximum_batch_bytes,
                                                                  ancestors.value());
            if (!valid.has_value()) {
                return valid;
            }
            const auto state = build_state_commitment(stored.value(),
                                                      proposal.header.section_root,
                                                      proposal.header.height,
                                                      proposal.parent_certificate);
            if (!state.has_value() || hash_state_commitment(state.value()) != proposal.header.state_commitment) {
                return std::unexpected(ConsensusError::InvalidRoot);
            }
            return admit_batch_intents(proposal, stored.value());
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
        const auto ancestors = staged_ancestors_for(proposal);
        if (!ancestors.has_value()) {
            return std::unexpected(ancestors.error());
        }
        const auto valid = node_.dag()->validate_shadow_batch(proposal,
                                                              local.value(),
                                                              consensus_->configuration().maximum_batch_bytes,
                                                              ancestors.value());
        if (!valid.has_value()) {
            return std::unexpected(valid.error());
        }
        const auto state = build_state_commitment(local.value(),
                                                  proposal.header.section_root,
                                                  proposal.header.height,
                                                  proposal.parent_certificate);
        if (!state.has_value() || hash_state_commitment(state.value()) != proposal.header.state_commitment) {
            return std::unexpected(ConsensusError::InvalidRoot);
        }
        const auto admitted = admit_batch_intents(proposal, local.value());
        if (!admitted.has_value()) {
            return admitted;
        }
        if (voting_enabled_) {
            return consensus_->engine().stage_batch_for_vote(std::move(local.value()));
        }
        return consensus_->engine().stage_batch(std::move(local.value()));
    }

    bool ConsensusService::has_unfinalized_intents() const {
        if (!consensus_) {
            return false;
        }
        const auto& state = consensus_->engine().safety_state();
        if (!state.highest_certificate.has_value()) {
            return false;
        }
        auto certificate = state.highest_certificate.value();
        while (certificate.phase != Phase::Genesis && certificate.height > state.finalized_height) {
            const auto proposal = consensus_->engine().proposal_for(certificate.header_hash);
            if (!proposal.has_value()) {
                return true;
            }
            if (!proposal.value().batch.transaction_hashes.empty()) {
                return true;
            }
            certificate = proposal.value().parent_certificate;
        }
        return false;
    }

    std::expected<std::vector<std::pair<IntentEnvelope, IntentReceipt>>, ConsensusError> ConsensusService::
        finalized_intents(const Proposal& proposal, const SectionBatchData& batch) const {
        std::vector<std::pair<IntentEnvelope, IntentReceipt>> result;
        WireFormat::Scope                                     canonical_scope(WireFormat::Mode::Canonical);
        for (const auto& [section, bytes] : batch.sections) {
            const auto decoded = Json::deserialize<Section>(bytes);
            if (!decoded.has_value()) {
                return std::unexpected(ConsensusError::InvalidIntent);
            }
            std::uint32_t position = 0;
            for (const auto& transaction : decoded.value().transactions) {
                if (!transaction.consensus_intent().has_value()) {
                    ++position;
                    continue;
                }
                const auto envelope = intent_from_transaction(transaction);
                if (!envelope.has_value()) {
                    return std::unexpected(envelope.error());
                }
                result.emplace_back(envelope.value(),
                                    IntentReceipt {
                                        .intent_hash      = hash_intent(envelope.value().intent),
                                        .status           = IntentStatus::Finalized,
                                        .consensus_height = proposal.header.height,
                                        .dag_section      = section,
                                        .position         = position,
                                        .error            = ConsensusError::NotReady,
                                    });
                ++position;
            }
        }
        return result;
    }

    void ConsensusService::evict_unprovable_intents(const Proposal& proposal, const SectionBatchData& batch) {
        const auto ancestors = staged_ancestors_for(proposal);
        if (!ancestors.has_value()) {
            // A broken ancestor chain is a consensus problem; blaming intents for it
            // would evict valid work.
            return;
        }
        const auto               unprovable = node_.dag()->unprovable_batch_transactions(batch, ancestors.value());
        std::vector<std::string> rejected;
        rejected.reserve(unprovable.size());
        for (const auto& transaction : unprovable) {
            if (!transaction.consensus_intent().has_value()) {
                continue;
            }
            const auto envelope = intent_from_transaction(transaction);
            if (!envelope.has_value()) {
                continue;
            }
            rejected.push_back(hash_intent(envelope.value().intent));
        }
        if (rejected.empty()) {
            return;
        }
        if (!intent_store_) {
            eCritical("[Shadow] Cannot persist rejection receipts because the intent store is unavailable");
            halt_voting();
            return;
        }
        const auto stored = intent_store_->reject(rejected, ConsensusError::InvalidIntent);
        if (!stored.has_value()) {
            eCritical("[Shadow] Could not persist rejection receipts for {} intents: {}",
                      rejected.size(),
                      std::to_underlying(stored.error()));
            halt_voting();
            return;
        }
        intent_pool_.erase(rejected);
        // The staged batch would keep re-proposing the same rejected intents; drop it
        // so the next queue_next_checkpoint materializes a fresh one without them.
        pending_checkpoints_.erase(proposal.batch.last_section);
        pending_batches_.erase(proposal.batch.last_section);
        eWarning("[Shadow] Evicted {} unprovable intents; batch for section {} will be rebuilt",
                 rejected.size(),
                 proposal.batch.last_section);
        queue_next_checkpoint();
    }

    std::expected<void, ConsensusError> ConsensusService::admit_batch_intents(const Proposal&         proposal,
                                                                              const SectionBatchData& batch) {
        const auto finalized = finalized_intents(proposal, batch);
        if (!finalized.has_value()) {
            return std::unexpected(finalized.error());
        }
        if (proposal.header.height >= consensus_->configuration().activation_height
            && finalized.value().size() != proposal.batch.transaction_hashes.size()) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }
        bool epoch_change_seen = false;
        for (const auto& [envelope, _] : finalized.value()) {
            if (envelope.intent.operation == IntentOperation::EpochChange) {
                const auto bytes = Utils::from_base64(envelope.metadata);
                if (epoch_change_seen || !bytes.has_value()
                    || envelope.intent.receiver != consensus_->engine().validators().document().network_id) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                const auto request = MessagePack::deserialize<EpochChangeRequestV1>(bytes.value());
                if (!request.has_value()
                    || !consensus_->validate_epoch_request(request.value(), proposal.header.height).has_value()) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                epoch_change_seen = true;
            }
            const auto accepted = accept_intent(envelope, false);
            if (!accepted.has_value() || accepted.value() != hash_intent(envelope.intent)) {
                return std::unexpected(accepted.has_value() ? ConsensusError::InvalidIntent : accepted.error());
            }
        }
        return {};
    }

    std::expected<void, ConsensusError> ConsensusService::process_epoch_changes(
        const std::vector<std::pair<IntentEnvelope, IntentReceipt>>& finalized) {
        for (const auto& [envelope, receipt] : finalized) {
            if (envelope.intent.operation != IntentOperation::EpochChange) {
                continue;
            }
            const auto bytes = Utils::from_base64(envelope.metadata);
            if (!bytes.has_value()) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            const auto request = MessagePack::deserialize<EpochChangeRequestV1>(bytes.value());
            if (!request.has_value() || request.value().protocol_version != ProtocolVersion
                || envelope.intent.receiver != consensus_->engine().validators().document().network_id
                || receipt.status != IntentStatus::Finalized) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            const auto action_hash = epoch_change_action_hash(request.value().change);
            if (consensus_->pending_epoch().has_value()
                && consensus_->pending_epoch().value().bootstrap.epoch_change_hash == action_hash) {
                continue;
            }
            const auto proof = consensus_->engine().transaction_inclusion_proof(action_hash);
            if (!proof.has_value() || !proof.value().has_value()) {
                return std::unexpected(ConsensusError::InvalidProof);
            }
            const auto scheduled = consensus_->schedule_epoch(request.value().change,
                                                              request.value().next_validators,
                                                              proof.value().value());
            if (!scheduled.has_value()) {
                return std::unexpected(scheduled.error());
            }
        }
        return {};
    }

    std::expected<bool, ConsensusError> ConsensusService::activate_pending_epoch() {
        const auto activated = consensus_->activate_scheduled_epoch();
        if (!activated.has_value()) {
            halt_voting();
            return std::unexpected(activated.error());
        }
        if (!activated.value()) {
            return activated;
        }
        if (consensus_->engine().identity().has_value()) {
            const auto* local =
                consensus_->engine().validators().find(consensus_->engine().identity().value().validator_id);
            if (local == nullptr || local->node_identifier != node_.node_identifier()) {
                return std::unexpected(ConsensusError::InvalidValidator);
            }
        }
        authenticator_  = std::make_unique<PeerAuthenticator>(consensus_->engine().validators(),
                                                             consensus_->engine().identity());
        voting_enabled_ = consensus_->engine().identity().has_value();
        latest_proposal_.reset();
        latest_certificate_.reset();
        latest_timeout_certificate_.reset();
        pending_checkpoints_.clear();
        pending_batches_.clear();
        pending_proposals_.clear();
        reset_timeout();
        eInfo("[Shadow] Activated validator epoch {} at height {}",
              consensus_->engine().validators().document().epoch,
              consensus_->engine().genesis_certificate().height + 1);
        return true;
    }

} // namespace ExtraChain::Consensus
