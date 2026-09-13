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
#include <chrono>
#include <map>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/signals2/connection.hpp>

#include "consensus/peer_authenticator.h"
#include "consensus/balance_snapshot.h"
#include "consensus/mining_replay.h"
#include "consensus/relay_transport.h"
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
        friend class ConsensusStateTestFixture;

    public:
        explicit ConsensusService(Core::ExtraChainNode& node, std::filesystem::path directory = "consensus");
        ~ConsensusService();

        ConsensusService(const ConsensusService&)            = delete;
        ConsensusService& operator=(const ConsensusService&) = delete;

        std::expected<bool, ConsensusError> activate(const ActorId& network_id);
        std::expected<void, ConsensusError> prepare_observer_bootstrap(const ActorId& network_id);
        void                                deactivate();

        void receive_network_message(MessageType        type,
                                     MessageStatus      status,
                                     const std::string& serialized,
                                     const Responder&   responder,
                                     std::string_view   peer_identifier);

        [[nodiscard]] bool should_drop_duplicate_relay(std::string_view serialized) const;

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
        // Allocate a nonce and sign under the submission lock, including concurrent local mining work.
        std::expected<std::string, ConsensusError> submit_local_intent(TransactionIntentV2      intent,
                                                                       std::string              metadata,
                                                                       const Actor<KeyPrivate>& sender);
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

        std::expected<BalanceSnapshotV1, ConsensusError> balance_snapshot() const;
        bool accept_balance_snapshot(const BalanceSnapshotV1& snapshot);

        [[nodiscard]] bool                                       native_mining_enabled() const;
        [[nodiscard]] std::expected<MiningState, ConsensusError> finalized_mining_state() const;
        // Preview the next section for local proof work; this does not commit state or issue coins.
        [[nodiscard]] std::expected<MiningState, ConsensusError> mining_work_state() const;
        std::expected<std::string, ConsensusError> submit_mining_request(IntentOperation          operation,
                                                                         std::string              metadata,
                                                                         const Actor<KeyPrivate>& provider);
        // Fill an unused local nonce before already signed pending requests. No asset effect.
        std::expected<bool, ConsensusError> repair_local_nonce_gap(const Actor<KeyPrivate>& sender);
        [[nodiscard]] bool verify_mining_transaction(const Transaction& transaction) const;
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
        struct RelayContext {
            std::string validator_id;
            std::string node_identifier;
            std::string request_id;
        };
        RelayTransport              relay_transport_;
        std::optional<RelayContext> relay_context_;
        std::optional<std::string>  authenticated_sender(std::string_view identifier) const;
        void                        receive_relay(const RelayEnvelope& envelope, std::string_view peer_identifier);
        bool send_relay(MessageType type, MessageStatus status, std::string payload, std::string destination = {});
        void dispatch_message(MessageType        type,
                              MessageStatus      status,
                              const std::string& serialized,
                              const Responder&   responder,
                              std::string_view   peer_identifier);
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
        void                                discard_stale_checkpoints(std::uint64_t next_section);
        void request_batch(const Proposal& proposal, std::string_view peer_identifier);
        /// Stop voting over a failure that may pass — a write that did not land, a
        /// checkpoint we could not apply yet. Unlike halt_voting() the pacemaker
        /// keeps running, so the node retries instead of needing a restart.
        void pause_voting(std::string_view reason);
        /// Apply one finalized checkpoint: reconcile it with the DAG in finality
        /// mode, publish it otherwise. Returns false when the checkpoint could not
        /// be applied and voting had to stop.
        bool apply_finalized_checkpoint(const FinalizedCheckpoint& checkpoint);
        /// Drive checkpoints that were deferred for missing data to completion.
        void catch_up_deferred_finalization();
        /// Request a missing ancestor from one validator, rotating peers on retry.
        void request_ancestor_batch(const std::string& header_hash, std::string_view peer_identifier);
        void request_sync_from(std::string_view peer_identifier);
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
        [[nodiscard]] std::expected<void, ConsensusError> validate_proposal(
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
            const QuorumCertificate&     parent,
            std::uint64_t                first_section,
            std::string*                 missing_ancestor = nullptr,
            std::optional<std::uint64_t> applied_height   = std::nullopt) const;
        /// Ancestors as a set, ready for the DAG's balance proofs. An empty set means
        /// the parent is already canonical; a broken ancestor chain is an error, not
        /// an empty set, so a proposal is never accepted on a silently weaker check.
        [[nodiscard]] std::expected<std::set<Transaction>, ConsensusError> staged_ancestors_for(
            const Proposal& proposal,
            std::string*    missing_ancestor = nullptr) const;
        std::expected<std::map<ActorId, std::uint64_t>, ConsensusError> staged_nonces_for(
            const QuorumCertificate& parent,
            std::uint64_t            first_section,
            std::string*             missing_ancestor = nullptr) const;
        [[nodiscard]] std::expected<std::map<ActorId, std::uint64_t>, ConsensusError> local_nonce_frontier(
            std::string* missing_ancestor = nullptr) const;
        std::expected<void, ConsensusError>                                           restore_pending_intents();
        std::expected<void, ConsensusError> expire_pending_intents(const std::map<ActorId, std::uint64_t>& nonces);
        [[nodiscard]] std::expected<std::uint64_t, ConsensusError> next_local_nonce(const ActorId& sender);
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

        bool accept_light_history(const BootstrapHistoryPageV1& page);
        void request_light_history();
        std::expected<LightClientVerifier, ConsensusError> load_light_verifier() const;

        struct MiningSnapshot {
            std::string header_hash;
            MiningState state;
            MSGPACK_DEFINE(header_hash, state)
        };
        std::expected<const LightClientVerifier*, ConsensusError> mining_verifier() const;
        std::expected<FinalityProof, ConsensusError>              mining_finality(std::uint64_t section) const;
        std::expected<void, ConsensusError>                       initialize_mining_state() const;
        std::expected<MiningState, ConsensusError>                mining_state_for(const QuorumCertificate& parent,
                                                                                   std::size_t              depth = 0) const;
        std::expected<MiningState, ConsensusError> project_mining_state(const SectionBatchData&  batch,
                                                                        const QuorumCertificate& parent) const;
        std::expected<void, ConsensusError>        persist_mining_state(const FinalityProof&    proof,
                                                                        const SectionBatchData& batch);
        std::expected<std::optional<Transaction>, ConsensusError> next_mining_settlement(
            const MiningState& parent,
            std::uint64_t      first_section) const;
        mutable std::optional<LightClientVerifier> mining_verifier_;
        mutable std::optional<MiningSnapshot>      finalized_mining_;
        mutable std::map<std::string, MiningState> staged_mining_;

        Core::ExtraChainNode&                                         node_;
        std::optional<LightClientVerifier>                            light_verifier_;
        std::filesystem::path                                         directory_;
        std::unique_ptr<ShadowConsensus>                              consensus_;
        std::unique_ptr<IntentStore>                                  intent_store_;
        IntentPool                                                    intent_pool_;
        bool                                                          pending_intents_restored_ = false;
        std::map<ActorId, std::uint64_t>                              committed_nonces_;
        struct NonceFrontier {
            std::string                      certificate_hash;
            std::map<ActorId, std::uint64_t> nonces;
        };
        mutable std::optional<NonceFrontier>                          nonce_frontier_;
        std::optional<AppliedCheckpoint>                              applied_checkpoint_;
        std::unique_ptr<PeerAuthenticator>                            authenticator_;
        std::optional<Proposal>                                       latest_proposal_;
        std::optional<QuorumCertificate>                              latest_certificate_;
        std::optional<TimeoutCertificate>                             latest_timeout_certificate_;
        std::map<std::uint64_t, ShadowCheckpoint>                     pending_checkpoints_;
        std::map<std::uint64_t, SectionBatchData>                     pending_batches_;
        std::map<std::string, Proposal>                               pending_proposals_;
        /// Last time an ancestor batch was asked for, by header hash, and the last
        /// sync request: a lagging node used to re-ask on every reply it got.
        struct AncestorRequest {
            std::chrono::steady_clock::time_point sent;
            std::string                           peer;
        };
        std::map<std::string, AncestorRequest>                        ancestor_requests_;
        std::chrono::steady_clock::time_point                         last_sync_request_ {};
        std::chrono::steady_clock::time_point                         last_light_history_request_ { };
        std::shared_ptr<Core::DeadlineTask>                           timeout_task_;
        std::shared_ptr<Core::DeadlineTask>                           recovery_task_;
        std::shared_ptr<Core::DeadlineTask>                           intent_batch_task_;
        std::vector<boost::signals2::scoped_connection>               connections_;
        Core::Event<const FinalizedCheckpoint&>                       finalized_event_;
        Core::Event<const ShadowBootstrapResponse&, std::string_view> bootstrap_event_;
        bool                                                          voting_enabled_ = false;
        /// Set when voting stopped over a transient failure; cleared once the
        /// pacemaker manages to resume. Never set for a deliberate halt.
        bool voting_paused_ { false };
        /// Guards discard_stale_checkpoints against re-entering itself.
        bool                                                          rebuilding_checkpoints_ { false };
        mutable std::recursive_mutex                                  mutex_;
    };

} // namespace ExtraChain::Consensus
