/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

#include <boost/signals2/connection.hpp>

#include "consensus/peer_authenticator.h"
#include "consensus/shadow_consensus.h"
#include "runtime/event.h"

class Responder;
enum class MessageType;
enum class MessageStatus;

namespace ExtraChain::Core {
    class ExtraChainNode;
}

namespace ExtraChain::Consensus {

    class EXTRACHAIN_EXPORT ConsensusService {
    public:
        explicit ConsensusService(Core::ExtraChainNode& node, std::filesystem::path directory = "consensus");
        ~ConsensusService();

        ConsensusService(const ConsensusService&)            = delete;
        ConsensusService& operator=(const ConsensusService&) = delete;

        std::expected<bool, ConsensusError> activate(const ActorId& network_id);
        void                                deactivate();

        void receive_network_message(MessageType        type,
                                     MessageStatus      status,
                                     const std::string& serialized,
                                     const Responder&   responder,
                                     std::string_view   peer_identifier);

        void receive_challenge(const AuthenticationChallenge& challenge, const Responder& responder);
        void receive_authentication(const AuthenticationResponse& response, std::string_view peer_identifier);
        void receive_proposal(const Proposal& proposal, std::string_view peer_identifier);
        void receive_vote(const Vote& vote, std::string_view peer_identifier);
        void receive_certificate(const QuorumCertificate& certificate, std::string_view peer_identifier);

        [[nodiscard]] bool                                     active() const noexcept;
        [[nodiscard]] bool                                     voting() const noexcept;
        [[nodiscard]] Core::Event<const FinalizedCheckpoint&>& finalized_event() noexcept;

    private:
        void peer_connected(const std::string& identifier);
        void checkpoint_ready(std::uint64_t section);
        bool apply_certificate(const QuorumCertificate& certificate);
        void send_to_peer(const auto& payload, MessageType message_type, const std::string& identifier);
        void send_to_validators(const auto& payload, MessageType message_type);
        [[nodiscard]] bool                       root_matches(const ConsensusHeader& header) const;
        [[nodiscard]] std::optional<std::string> transaction_root(std::uint64_t section) const;

        Core::ExtraChainNode&                           node_;
        std::filesystem::path                           directory_;
        std::unique_ptr<ShadowConsensus>                consensus_;
        std::unique_ptr<PeerAuthenticator>              authenticator_;
        std::optional<Proposal>                         latest_proposal_;
        std::optional<QuorumCertificate>                latest_certificate_;
        std::vector<boost::signals2::scoped_connection> connections_;
        Core::Event<const FinalizedCheckpoint&>         finalized_event_;
        mutable std::recursive_mutex                    mutex_;
    };

} // namespace ExtraChain::Consensus
