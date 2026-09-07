#include "consensus/consensus_service.h"

#include "core/extrachain_node.h"
#include "network/network_service.h"
#include "network/responder.h"
#include "utils/exc_utils.h"

namespace ExtraChain::Consensus {
    std::optional<std::string> ConsensusService::authenticated_sender(std::string_view identifier) const {
        if (relay_context_.has_value() && relay_context_.value().node_identifier == identifier) {
            return relay_context_.value().validator_id;
        }
        return authenticator_ ? authenticator_->authenticated_validator(identifier) : std::nullopt;
    }

    bool ConsensusService::send_relay(MessageType   type,
                                      MessageStatus status,
                                      std::string   payload,
                                      std::string   destination) {
        std::lock_guard lock(mutex_);
        switch (type) {
        case MessageType::ConsensusIntent:
        case MessageType::ConsensusProposal:
        case MessageType::ConsensusVote:
        case MessageType::ConsensusCertificate:
        case MessageType::ConsensusTimeoutVote:
        case MessageType::ConsensusTimeoutCertificate:
        case MessageType::ConsensusBatchRequest:
        case MessageType::ConsensusBatchData:
        case MessageType::ConsensusSyncRequest:
        case MessageType::ConsensusSyncResponse:
        case MessageType::ConsensusRecovery:
            break;
        default:
            return false;
        }
        if (!consensus_ || !consensus_->engine().identity().has_value()) {
            return false;
        }
        if (!destination.empty()) {
            const auto& validators = consensus_->engine().validators().active();
            if (std::ranges::none_of(validators, [&](const auto& validator) {
                    return validator.node_identifier == destination;
                })) {
                return false;
            }
        }
        const auto now      = static_cast<std::uint64_t>(Utils::current_date_ms());
        auto       envelope = RelayTransport::create(consensus_->engine().validators(),
                                               consensus_->engine().identity().value(),
                                               type,
                                               status,
                                               std::move(payload),
                                               std::move(destination),
                                               status == MessageStatus::Response && relay_context_.has_value()
                                                         ? relay_context_.value().request_id
                                                         : std::string {},
                                               now);
        if (!envelope.has_value()) {
            eWarning("[Shadow] Cannot create relay message: {}", std::to_underlying(envelope.error()));
            return true;
        }
        const auto connected = node_.network()->active_full_peers_with_capability(SHADOW_RELAY_CAPABILITY);
        const std::vector<std::string> peers(connected.begin(), connected.end());
        const auto                     delivery =
            relay_transport_
                .accept(envelope.value(),
                        consensus_->engine().validators(),
                        node_.node_identifier(),
                        {},
                        peers,
                        now,
                        authenticator_
                            && authenticator_->authenticated_validator(envelope.value().destination).has_value());
        if (!delivery.peers.empty()) {
            Responder responder(node_.network());
            for (const auto& peer : delivery.peers) {
                responder.add_identifier(peer);
            }
            node_.network()->send_message(envelope.value(),
                                          MessageType::ConsensusRelay,
                                          SendMode::Focused,
                                          MessageStatus::NoStatus,
                                          responder);
        }
        return true;
    }

    bool ConsensusService::should_drop_duplicate_relay(std::string_view serialized) const {
        if (serialized.size() > RelayTransport::MaximumDuplicateBytes) {
            return false;
        }
        std::lock_guard lock(mutex_);
        return consensus_
               && relay_transport_.should_drop_duplicate(serialized,
                                                         static_cast<std::uint64_t>(Utils::current_date_ms()));
    }

    void ConsensusService::receive_relay(const RelayEnvelope& envelope, std::string_view peer_identifier) {
        std::lock_guard lock(mutex_);
        if (!consensus_) {
            return;
        }
        const auto now       = static_cast<std::uint64_t>(Utils::current_date_ms());
        const auto connected = node_.network()->active_full_peers_with_capability(SHADOW_RELAY_CAPABILITY);
        const std::vector<std::string> peers(connected.begin(), connected.end());
        const auto                     delivery =
            relay_transport_
                .accept(envelope,
                        consensus_->engine().validators(),
                        node_.node_identifier(),
                        peer_identifier,
                        peers,
                        now,
                        authenticator_
                            && authenticator_->authenticated_validator(envelope.destination).has_value());
        if (!delivery.peers.empty()) {
            auto forwarded = envelope;
            ++forwarded.hops;
            Responder responder(node_.network());
            for (const auto& peer : delivery.peers) {
                responder.add_identifier(peer);
            }
            node_.network()->send_message(forwarded,
                                          MessageType::ConsensusRelay,
                                          SendMode::Focused,
                                          MessageStatus::NoStatus,
                                          responder);
        }
        if (!delivery.deliver) {
            return;
        }
        const auto* author = consensus_->engine().validators().find(envelope.origin);
        if (author == nullptr) {
            return;
        }
        struct RestoreContext {
            std::optional<RelayContext>& current;
            std::optional<RelayContext>  previous;
            ~RestoreContext() {
                current = std::move(previous);
            }
        } restore { relay_context_, relay_context_ };
        const std::string author_identifier = author->node_identifier;
        relay_context_ = RelayContext { envelope.origin, author_identifier, envelope.request_id };
        Responder responder(node_.network());
        responder.add_identifier(std::string(peer_identifier));
        dispatch_message(envelope.type, envelope.status, envelope.payload, responder, author_identifier);
    }
} // namespace ExtraChain::Consensus
