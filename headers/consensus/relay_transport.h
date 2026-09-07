#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "consensus/consensus_engine.h"
#include "network/message_body.h"

namespace ExtraChain::Consensus {

    struct RelayEnvelope {
        std::uint16_t version = 1;
        ActorId       network_id;
        std::uint64_t epoch = 0;
        std::string   origin;
        std::string   destination;
        MessageType   type   = MessageType::ConsensusProposal;
        MessageStatus status = MessageStatus::NoStatus;
        std::string   request_id;
        std::uint64_t expires_ms = 0;
        std::string   payload_hash;
        std::string   payload;
        std::string   signature;
        std::uint8_t  hops = 0;

        MSGPACK_DEFINE(version,
                       network_id,
                       epoch,
                       origin,
                       destination,
                       type,
                       status,
                       request_id,
                       expires_ms,
                       payload_hash,
                       payload,
                       signature,
                       hops)
    };

    struct RelayDelivery {
        bool                     deliver = false;
        std::vector<std::string> peers;
    };

    class EXTRACHAIN_EXPORT RelayTransport {
    public:
        static constexpr std::size_t  MaximumBytes = 68ULL * 1024 * 1024;
        static constexpr std::size_t  MaximumDuplicateBytes = 4096;
        static constexpr std::uint8_t MaximumHops  = 16;

        static std::expected<RelayEnvelope, ConsensusError> create(const ValidatorSetView&  validators,
                                                                   const ValidatorIdentity& identity,
                                                                   MessageType              type,
                                                                   MessageStatus            status,
                                                                   std::string              payload,
                                                                   std::string              destination,
                                                                   std::string              request_id,
                                                                   std::uint64_t            now_ms);
        static std::string                                  identifier(const RelayEnvelope& envelope);
        static bool valid(const RelayEnvelope& envelope, const ValidatorSetView& validators, std::uint64_t now_ms);
        [[nodiscard]] bool should_drop_duplicate(std::string_view serialized, std::uint64_t now_ms) const;
        RelayDelivery accept(const RelayEnvelope&            envelope,
                             const ValidatorSetView&         validators,
                             std::string_view                local_identifier,
                             std::string_view                incoming_peer,
                             const std::vector<std::string>& peers,
                             std::uint64_t                   now_ms,
                             bool                            destination_authenticated = false);
        void          clear();

    private:
        struct Route {
            std::string   peer;
            std::string   requester;
            std::string   destination;
            MessageType   request_type;
            std::uint64_t expires_ms;
        };
        std::map<std::string, std::uint64_t>                         seen_;
        std::map<std::string, Route>                                 routes_;
        std::map<std::string, std::pair<std::uint64_t, std::size_t>> rates_;
    };
} // namespace ExtraChain::Consensus
