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
        auto loaded = ShadowConsensus::load(directory_, network_id, [this](const ConsensusHeader& header) {
            return root_matches(header);
        });
        if (!loaded.has_value()) {
            return std::unexpected(loaded.error());
        }
        consensus_ = std::move(loaded.value());
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
        return true;
    }

    void ConsensusService::deactivate() {
        std::lock_guard lock(mutex_);
        connections_.clear();
        authenticator_.reset();
        consensus_.reset();
        latest_proposal_.reset();
        latest_certificate_.reset();
    }

    void ConsensusService::receive_network_message(MessageType        type,
                                                   MessageStatus      status,
                                                   const std::string& serialized,
                                                   const Responder&   responder,
                                                   std::string_view   peer_identifier) {
        const auto meta = node_.network()->peer_meta_for(std::string(peer_identifier));
        if (!meta.has_value() || !meta.value().supports_identity_bft_shadow()) {
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
            send_to_peer(latest_proposal_.value(), MessageType::ConsensusProposal, std::string(peer_identifier));
        }
        if (latest_certificate_.has_value()) {
            send_to_peer(latest_certificate_.value(),
                         MessageType::ConsensusCertificate,
                         std::string(peer_identifier));
        }
    }

    void ConsensusService::receive_proposal(const Proposal& proposal, std::string_view peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_ || !authenticator_
            || !authenticator_->is_authenticated(peer_identifier, proposal.proposer_id)) {
            return;
        }
        const auto vote = consensus_->receive_proposal(proposal, peer_identifier);
        if (!vote.has_value()) {
            eWarning("[Consensus] Proposal {} at height {} was rejected with error {}",
                     hash_header(proposal.header),
                     proposal.header.height,
                     std::to_underlying(vote.error()));
            return;
        }
        if (vote.value().has_value()) {
            send_to_peer(vote.value().value(), MessageType::ConsensusVote, std::string(peer_identifier));
        }
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
        if (finalized.value().has_value()) {
            finalized_event_.publish(finalized.value().value());
            eInfo("[Consensus] Shadow-finalized height {} at DAG section {}",
                  finalized.value().value().height,
                  finalized.value().value().dag_section);
        }
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
        if (!meta.has_value() || !meta.value().supports_identity_bft_shadow()) {
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
    }

    void ConsensusService::checkpoint_ready(std::uint64_t section) {
        std::lock_guard lock(mutex_);
        if (!consensus_) {
            return;
        }
        const auto control      = node_.dag()->read_control(SectionId(section));
        const auto transactions = transaction_root(section);
        if (!control.has_value() || !transactions.has_value()) {
            return;
        }
        const auto proposal =
            consensus_->make_checkpoint_proposal(ShadowCheckpoint { .dag_section      = section,
                                                                    .section_root     = control.value().control,
                                                                    .transaction_root = transactions.value() });
        if (proposal.has_value() && proposal.value().has_value()) {
            const auto self_vote = consensus_->engine().accept_proposal(proposal.value().value());
            if (!self_vote.has_value()) {
                eWarning("[Consensus] Cannot persist the leader vote at DAG section {}", section);
                return;
            }
            const auto accepted = consensus_->engine().accept_vote(self_vote.value());
            if (!accepted.has_value()) {
                eWarning("[Consensus] Cannot accept the leader vote at DAG section {}", section);
                return;
            }
            latest_proposal_ = proposal.value();
            send_to_validators(proposal.value().value(), MessageType::ConsensusProposal);
        }
    }

    void ConsensusService::send_to_peer(const auto&        payload,
                                        MessageType        message_type,
                                        const std::string& identifier) {
        Responder responder(node_.network());
        responder.add_identifier(identifier);
        node_.network()->send_message(payload,
                                      message_type,
                                      SendMode::Focused,
                                      MessageStatus::NoStatus,
                                      responder);
    }

    void ConsensusService::send_to_validators(const auto& payload, MessageType message_type) {
        for (const auto& identifier :
             node_.network()->active_full_peers_with_capability(IDENTITY_BFT_SHADOW_CAPABILITY)) {
            send_to_peer(payload, message_type, identifier);
        }
    }

    bool ConsensusService::root_matches(const ConsensusHeader& header) const {
        const auto control      = node_.dag()->read_control(SectionId(header.dag_section));
        const auto transactions = transaction_root(header.dag_section);
        return control.has_value() && control.value().control == header.section_root && transactions.has_value()
               && transactions.value() == header.transaction_root;
    }

    std::optional<std::string> ConsensusService::transaction_root(std::uint64_t section) const {
        const auto end   = SectionId(section);
        const auto start = section == 0 ? SectionId(0) : end - SectionId(CONTROL_INTERVAL - 1);
        return node_.dag()->hash_interval(start, end);
    }

} // namespace ExtraChain::Consensus
