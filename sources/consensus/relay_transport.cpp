#include "consensus/relay_transport.h"

#include "utils/exc_utils.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::size_t   MaximumControlBytes = 1024 * 1024;
        constexpr std::size_t   MaximumEntries      = 8192;
        constexpr std::uint64_t LifetimeMs          = 120'000;

        std::string signing_payload(const RelayEnvelope& value) {
            WireFormat::Scope canonical(WireFormat::Mode::Canonical);
            return "EXC_SHADOW_RELAY_V1"
                   + MessagePack::serialize(std::tuple { value.version,
                                                         value.network_id,
                                                         value.epoch,
                                                         value.origin,
                                                         value.destination,
                                                         value.type,
                                                         value.status,
                                                         value.request_id,
                                                         value.expires_ms,
                                                         value.payload_hash });
        }

        bool allowed(MessageType type, MessageStatus status) {
            switch (type) {
            case MessageType::ConsensusIntent:
            case MessageType::ConsensusProposal:
            case MessageType::ConsensusVote:
            case MessageType::ConsensusCertificate:
            case MessageType::ConsensusTimeoutVote:
            case MessageType::ConsensusTimeoutCertificate:
            case MessageType::ConsensusRecovery:
                return status == MessageStatus::NoStatus;
            case MessageType::ConsensusBatchRequest:
            case MessageType::ConsensusSyncRequest:
                return status == MessageStatus::Request;
            case MessageType::ConsensusBatchData:
            case MessageType::ConsensusSyncResponse:
                return status == MessageStatus::Response;
            default:
                return false;
            }
        }

        const ValidatorRecord* node_record(const ValidatorSetView& validators, std::string_view node) {
            for (const auto& record : validators.active()) {
                if (record.node_identifier == node) {
                    return &record;
                }
            }
            return nullptr;
        }
    } // namespace

    std::string RelayTransport::identifier(const RelayEnvelope& envelope) {
        return Utils::calculate_hash(signing_payload(envelope), Utils::HashAlgorithm::Blake3);
    }

    std::expected<RelayEnvelope, ConsensusError> RelayTransport::create(const ValidatorSetView&  validators,
                                                                        const ValidatorIdentity& identity,
                                                                        MessageType              type,
                                                                        MessageStatus            status,
                                                                        std::string              payload,
                                                                        std::string              destination,
                                                                        std::string              request_id,
                                                                        std::uint64_t            now_ms) {
        RelayEnvelope envelope;
        envelope.network_id  = validators.document().network_id;
        envelope.epoch       = validators.document().epoch;
        envelope.origin      = identity.validator_id;
        envelope.destination = std::move(destination);
        envelope.type        = type;
        envelope.status      = status;
        envelope.request_id  = std::move(request_id);
        if (status == MessageStatus::Request) {
            envelope.request_id = Utils::generate_random_hex(32);
        }
        envelope.expires_ms   = now_ms + LifetimeMs;
        envelope.payload_hash = Utils::calculate_hash(payload, Utils::HashAlgorithm::Blake3);
        envelope.payload      = std::move(payload);
        const auto signature  = sign_payload(identity.key, signing_payload(envelope));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        envelope.signature = signature.value();
        if (!valid(envelope, validators, now_ms)) {
            return std::unexpected(ConsensusError::InvalidProtocol);
        }
        return envelope;
    }

    bool RelayTransport::valid(const RelayEnvelope&    envelope,
                               const ValidatorSetView& validators,
                               std::uint64_t           now_ms) {
        if (envelope.version != 1 || envelope.network_id != validators.document().network_id
            || envelope.epoch != validators.document().epoch || envelope.hops >= MaximumHops
            || envelope.expires_ms <= now_ms || envelope.expires_ms - now_ms > LifetimeMs + 30'000
            || !allowed(envelope.type, envelope.status) || envelope.origin.size() > 128
            || envelope.destination.size() > 128 || envelope.request_id.size() > 128
            || envelope.payload_hash.size() != 64 || envelope.signature.size() > 128) {
            return false;
        }
        const bool response = envelope.status == MessageStatus::Response;
        if (envelope.payload.size() > (response ? MaximumBytes : MaximumControlBytes)
            || (envelope.status != MessageStatus::NoStatus
                && (envelope.request_id.empty() || envelope.destination.empty()))
            || (!envelope.destination.empty() && node_record(validators, envelope.destination) == nullptr)) {
            return false;
        }
        const auto* author = validators.find(envelope.origin);
        return author != nullptr && author->status == ValidatorStatus::Active
               && envelope.payload_hash == Utils::calculate_hash(envelope.payload, Utils::HashAlgorithm::Blake3)
               && verify_payload(author->consensus_public_key, signing_payload(envelope), envelope.signature);
    }

    bool RelayTransport::should_drop_duplicate(std::string_view serialized, std::uint64_t now_ms) const {
        if (seen_.empty() || serialized.empty() || serialized.size() > MaximumDuplicateBytes) {
            return false;
        }
        try {
            // This path runs before outer authentication. Bound decoded allocations too.
            const auto envelope =
                msgpack::unpack(serialized.data(),
                                serialized.size(),
                                nullptr,
                                nullptr,
                                msgpack::unpack_limit(16, 16, MaximumDuplicateBytes, MaximumDuplicateBytes, 0, 4))
                    .get()
                    .as<RelayEnvelope>();
            if (envelope.origin.size() > 128 || envelope.destination.size() > 128
                || envelope.request_id.size() > 128 || envelope.payload_hash.size() != 64) {
                return false;
            }
            const auto seen = seen_.find(identifier(envelope));
            // A hit permits only a drop, never delivery or a cache update.
            return seen != seen_.end() && seen->second > now_ms;
        } catch (const std::exception&) {
            return false;
        }
    }

    RelayDelivery RelayTransport::accept(const RelayEnvelope&            envelope,
                                         const ValidatorSetView&         validators,
                                         std::string_view                local_identifier,
                                         std::string_view                incoming_peer,
                                         const std::vector<std::string>& peers,
                                         std::uint64_t                   now_ms,
                                         bool                            destination_authenticated) {
        RelayDelivery result;
        if (envelope.origin.size() > 128 || envelope.destination.size() > 128 || envelope.request_id.size() > 128
            || envelope.payload_hash.size() != 64) {
            return result;
        }
        const auto id   = identifier(envelope);
        const auto seen = seen_.find(id);
        // Only a fully accepted envelope can suppress another copy. No copy
        // rejected here can deliver data or create a response route.
        if (seen != seen_.end() && seen->second > now_ms) {
            return result;
        }
        if (!valid(envelope, validators, now_ms)) {
            return result;
        }
        std::erase_if(seen_, [now_ms](const auto& entry) {
            return entry.second <= now_ms;
        });
        std::erase_if(routes_, [now_ms](const auto& entry) {
            return entry.second.expires_ms <= now_ms;
        });
        std::erase_if(rates_, [now_ms](const auto& entry) {
            return entry.second.first != now_ms / 1000;
        });
        if (seen_.contains(id) || seen_.size() >= MaximumEntries) {
            return result;
        }
        auto& rate = rates_[envelope.origin];
        rate.first = now_ms / 1000;
        if (++rate.second > 512) {
            return result;
        }
        const auto* author = validators.find(envelope.origin);
        if (envelope.status == MessageStatus::Response) {
            const auto route_key =
                MessagePack::serialize(std::tuple { envelope.destination, envelope.request_id });
            const auto route = routes_.find(route_key);
            if (route == routes_.end() || route->second.destination != author->node_identifier
                || route->second.requester != envelope.destination
                || (route->second.request_type == MessageType::ConsensusBatchRequest
                        ? envelope.type != MessageType::ConsensusBatchData
                        : envelope.type != MessageType::ConsensusSyncResponse)) {
                return result;
            }
            seen_.emplace(id, envelope.expires_ms);
            if (envelope.destination == local_identifier) {
                result.deliver = true;
            } else if (!route->second.peer.empty() && route->second.peer != incoming_peer
                       && envelope.hops + 1 < MaximumHops) {
                result.peers.push_back(route->second.peer);
            }
            return result;
        }
        if (envelope.status == MessageStatus::Request) {
            if (routes_.size() >= MaximumEntries) {
                return result;
            }
            const auto [route, inserted] =
                routes_.try_emplace(MessagePack::serialize(
                                        std::tuple { author->node_identifier, envelope.request_id }),
                                    Route { std::string(incoming_peer),
                                            author->node_identifier,
                                            envelope.destination,
                                            envelope.type,
                                            envelope.expires_ms });
            if (!inserted) {
                return result;
            }
        }
        seen_.emplace(id, envelope.expires_ms);
        result.deliver = envelope.destination.empty() || envelope.destination == local_identifier;
        if (envelope.destination != local_identifier && envelope.hops + 1 < MaximumHops) {
            // A claimed peer identifier alone cannot select the only delivery path.
            if (destination_authenticated && !envelope.destination.empty() && envelope.destination != incoming_peer
                && std::ranges::find(peers, envelope.destination) != peers.end()) {
                result.peers.push_back(envelope.destination);
                return result;
            }
            for (const auto& peer : peers) {
                if (peer != incoming_peer) {
                    result.peers.push_back(peer);
                }
            }
        }
        return result;
    }

    void RelayTransport::clear() {
        seen_.clear();
        routes_.clear();
        rates_.clear();
    }
} // namespace ExtraChain::Consensus
