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
#include <utility>
#include <vector>

#include <boost/signals2/connection.hpp>

#include "consensus/peer_authenticator.h"
#include "consensus/intent_store.h"
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
        void receive_bootstrap_request(const ShadowBootstrapRequest& request,
                                       const Responder&              responder,
                                       std::string_view              peer_identifier);
        void receive_recovery(const RecoveryRequestV1& request, std::string_view peer_identifier);
        void receive_intent(const IntentEnvelope& envelope);

        std::expected<std::string, ConsensusError> submit_intent(const IntentEnvelope& envelope);
        [[nodiscard]] std::vector<IntentEnvelope>  ready_intents(std::size_t maximum_count,
                                                                 std::size_t maximum_bytes) const;
        [[nodiscard]] std::expected<std::optional<IntentReceipt>, ConsensusError> intent_receipt(
            std::string_view intent_hash);
        std::expected<void, ConsensusError> finalize_intents(
            const std::vector<std::pair<IntentEnvelope, IntentReceipt>>& finalized,
            std::optional<AppliedCheckpoint>                             checkpoint = std::nullopt);
        std::expected<void, ConsensusError>        submit_recovery(const RecoveryDocumentV2& recovery,
                                                                   ValidatorSet              next_validators,
                                                                   std::uint64_t             now_ms);
        std::expected<std::size_t, ConsensusError> request_bootstrap_history(const TrustAnchorV1& anchor,
                                                                             std::uint64_t        after_epoch);

        [[nodiscard]] bool active() const noexcept;
        [[nodiscard]] bool voting() const noexcept;
        [[nodiscard]] bool controls_section(std::uint64_t section) const;
        [[nodiscard]] bool requires_intent_v2() const noexcept;
        bool               repair_section(std::uint64_t section);
        [[nodiscard]] std::expected<std::optional<TransactionInclusionProofV1>, ConsensusError>
                           transaction_inclusion_proof(std::string_view transaction_hash) const;
        [[nodiscard]] bool verify_transaction_inclusion_proof(const TransactionInclusionProofV1& proof) const;
        [[nodiscard]] ConsensusMetricsSnapshot                                       metrics() const noexcept;
        [[nodiscard]] Core::Event<const FinalizedCheckpoint&>&                       finalized_event() noexcept;
        [[nodiscard]] Core::Event<const ShadowBootstrapResponse&, std::string_view>& bootstrap_event() noexcept;

    private:
        void                                peer_connected(const std::string& identifier);
        void                                challenge_peer(const std::string& identifier, bool reset_existing);
        void                                refresh_peer_authentication();
        void                                checkpoint_ready(std::uint64_t section);
        void                                queue_next_checkpoint();
        bool                                apply_certificate(const QuorumCertificate& certificate);
        std::expected<void, ConsensusError> apply_finality_proof(const FinalityProof& proof);
        std::expected<void, ConsensusError> reconcile_finalized_checkpoint();
        bool                                apply_timeout_certificate(const TimeoutCertificate& certificate);
        void                                propose_checkpoint(std::uint64_t round);
        void request_batch(const Proposal& proposal, std::string_view peer_identifier);
        /// Ask every validator for a specific ancestor payload we are missing.
        void request_ancestor_batch(const std::string& header_hash);
        void vote_for_proposal(const Proposal& proposal, std::string_view peer_identifier);
        void timeout_elapsed();
        void reset_timeout();
        void halt_voting();
        void send_to_peer(const auto&        payload,
                          MessageType        message_type,
                          const std::string& identifier,
                          MessageStatus      status);
        void send_to_validators(const auto& payload, MessageType message_type);
        void send_to_validators(const auto& payload, MessageType message_type, MessageStatus status);
        [[nodiscard]] std::expected<void, ConsensusError>              validate_proposal(
            const Proposal& proposal,
            std::string*    missing_ancestor = nullptr);
        [[nodiscard]] std::expected<StateCommitmentV2, ConsensusError> build_state_commitment(
            const SectionBatchData&  batch,
            std::string_view         section_root,
            std::uint64_t            height,
            const QuorumCertificate& parent) const;
        /// \p missing_ancestor, when given, receives the header hash of the first
        /// ancestor whose batch we simply do not hold yet. Absent data and corrupt
        /// data both break the walk, but only the former is worth another request.
        [[nodiscard]] std::expected<std::vector<Transaction>, ConsensusError> staged_ancestor_transactions(
            const QuorumCertificate& parent,
            std::uint64_t            first_section,
            std::string*             missing_ancestor = nullptr) const;
        /// Ancestors as a set, ready for the DAG's balance proofs. An empty set means
        /// the parent is already canonical; a broken ancestor chain is an error, not
        /// an empty set, so a proposal is never accepted on a silently weaker check.
        [[nodiscard]] std::expected<std::set<Transaction>, ConsensusError> staged_ancestors_for(
            const Proposal& proposal,
            std::string*    missing_ancestor = nullptr) const;
        [[nodiscard]] bool                         has_unfinalized_intents() const;
        std::expected<std::string, ConsensusError> accept_intent(const IntentEnvelope& envelope, bool broadcast);
        [[nodiscard]] std::expected<std::vector<std::pair<IntentEnvelope, IntentReceipt>>, ConsensusError>
        finalized_intents(const Proposal& proposal, const SectionBatchData& batch) const;
        [[nodiscard]] std::expected<void, ConsensusError> admit_batch_intents(const Proposal&         proposal,
                                                                              const SectionBatchData& batch);
        void evict_unprovable_intents(const Proposal& proposal, const SectionBatchData& batch);
        std::expected<void, ConsensusError> process_epoch_changes(
            const std::vector<std::pair<IntentEnvelope, IntentReceipt>>& finalized);
        std::expected<bool, ConsensusError> activate_pending_epoch();
        std::expected<bool, ConsensusError> activate_pending_recovery(std::uint64_t now_ms);
        void                                schedule_recovery_activation();
        [[nodiscard]] std::uint64_t         intent_height() const noexcept;

        Core::ExtraChainNode&                                         node_;
        std::filesystem::path                                         directory_;
        std::unique_ptr<ShadowConsensus>                              consensus_;
        std::unique_ptr<IntentStore>                                  intent_store_;
        IntentPool                                                    intent_pool_;
        std::map<ActorId, std::uint64_t>                              committed_nonces_;
        std::optional<AppliedCheckpoint>                              applied_checkpoint_;
        std::unique_ptr<PeerAuthenticator>                            authenticator_;
        std::optional<Proposal>                                       latest_proposal_;
        std::optional<QuorumCertificate>                              latest_certificate_;
        std::optional<TimeoutCertificate>                             latest_timeout_certificate_;
        std::map<std::uint64_t, ShadowCheckpoint>                     pending_checkpoints_;
        std::map<std::uint64_t, SectionBatchData>                     pending_batches_;
        std::map<std::string, Proposal>                               pending_proposals_;
        std::shared_ptr<Core::DeadlineTask>                           timeout_task_;
        std::shared_ptr<Core::DeadlineTask>                           recovery_task_;
        std::shared_ptr<Core::DeadlineTask>                           intent_batch_task_;
        std::vector<boost::signals2::scoped_connection>               connections_;
        Core::Event<const FinalizedCheckpoint&>                       finalized_event_;
        Core::Event<const ShadowBootstrapResponse&, std::string_view> bootstrap_event_;
        bool                                                          voting_enabled_ = false;
        mutable std::recursive_mutex                                  mutex_;
    };

} // namespace ExtraChain::Consensus
