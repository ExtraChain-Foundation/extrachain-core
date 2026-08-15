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
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

#include <boost/signals2/connection.hpp>

#include "consensus/peer_authenticator.h"
#include "consensus/shadow_consensus.h"
#include "runtime/event.h"
#include "runtime/deadline_task.h"

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
        void receive_timeout_vote(const TimeoutVote& vote, std::string_view peer_identifier);
        void receive_timeout_certificate(const TimeoutCertificate& certificate, std::string_view peer_identifier);
        void receive_batch_request(const SectionBatchRequest& request,
                                   const Responder&           responder,
                                   std::string_view           peer_identifier);
        void receive_batch_data(const SectionBatchData& batch, std::string_view peer_identifier);
        void receive_sync_request(const ShadowSyncRequest& request,
                                  const Responder&         responder,
                                  std::string_view         peer_identifier);
        void receive_sync_response(const ShadowSyncResponse& response, std::string_view peer_identifier);

        [[nodiscard]] bool                                     active() const noexcept;
        [[nodiscard]] bool                                     voting() const noexcept;
        [[nodiscard]] bool                                     controls_section(std::uint64_t section) const;
        bool                                                   repair_section(std::uint64_t section);
        [[nodiscard]] Core::Event<const FinalizedCheckpoint&>& finalized_event() noexcept;

    private:
        void                                peer_connected(const std::string& identifier);
        void                                checkpoint_ready(std::uint64_t section);
        void                                queue_next_checkpoint();
        bool                                apply_certificate(const QuorumCertificate& certificate);
        std::expected<void, ConsensusError> reconcile_finalized_checkpoint();
        bool                                apply_timeout_certificate(const TimeoutCertificate& certificate);
        void                                propose_checkpoint(std::uint64_t round);
        void request_batch(const Proposal& proposal, std::string_view peer_identifier);
        void vote_for_proposal(const Proposal& proposal, std::string_view peer_identifier);
        void timeout_elapsed();
        void reset_timeout();
        void send_to_peer(const auto&        payload,
                          MessageType        message_type,
                          const std::string& identifier,
                          MessageStatus      status);
        void send_to_validators(const auto& payload, MessageType message_type);
        void send_to_validators(const auto& payload, MessageType message_type, MessageStatus status);
        [[nodiscard]] std::expected<void, ConsensusError> validate_proposal(const Proposal& proposal);

        Core::ExtraChainNode&                           node_;
        std::filesystem::path                           directory_;
        std::unique_ptr<ShadowConsensus>                consensus_;
        std::unique_ptr<PeerAuthenticator>              authenticator_;
        std::optional<Proposal>                         latest_proposal_;
        std::optional<QuorumCertificate>                latest_certificate_;
        std::optional<TimeoutCertificate>               latest_timeout_certificate_;
        std::map<std::uint64_t, ShadowCheckpoint>       pending_checkpoints_;
        std::map<std::string, Proposal>                 pending_proposals_;
        std::shared_ptr<Core::DeadlineTask>             timeout_task_;
        std::vector<boost::signals2::scoped_connection> connections_;
        Core::Event<const FinalizedCheckpoint&>         finalized_event_;
        mutable std::recursive_mutex                    mutex_;
    };

} // namespace ExtraChain::Consensus
