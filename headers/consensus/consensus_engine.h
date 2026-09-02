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

#include <expected>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

#include "consensus/consensus_protocol.h"
#include "consensus/safety_store.h"
#include "consensus/validator_set.h"

namespace ExtraChain::Consensus {

    struct ValidatorIdentity {
        std::string validator_id;
        KeyPrivate  key;
    };

    struct VoteAcceptance {
        std::optional<QuorumCertificate> certificate;
        std::optional<EquivocationProof> equivocation;
    };

    class EXTRACHAIN_EXPORT ConsensusEngine {
    public:
        using ProposalValidator = std::function<std::expected<void, ConsensusError>(const Proposal&)>;

        ConsensusEngine(ValidatorSetView                 validators,
                        std::optional<ValidatorIdentity> identity,
                        std::unique_ptr<SafetyStore>     store,
                        ProposalValidator                proposal_validator = {},
                        std::optional<EpochBootstrapV1>  epoch_bootstrap    = std::nullopt);

        std::expected<void, ConsensusError>           initialize();
        std::expected<Proposal, ConsensusError>       make_proposal(SectionBatchManifest batch,
                                                                    StateCommitmentV2    state,
                                                                    std::uint64_t        round = 0);
        std::expected<TimeoutVote, ConsensusError>    make_timeout_vote(std::uint64_t height, std::uint64_t round);
        std::expected<void, ConsensusError>           observe_proposal(const Proposal& proposal);
        std::expected<void, ConsensusError>           stage_batch(SectionBatchData batch);
        std::expected<void, ConsensusError>           stage_batch_for_vote(SectionBatchData batch);
        std::expected<Vote, ConsensusError>           accept_proposal(const Proposal& proposal);
        std::expected<VoteAcceptance, ConsensusError> accept_vote(const Vote& vote);
        std::expected<TimeoutAcceptance, ConsensusError> accept_timeout_vote(const TimeoutVote& vote);
        std::expected<void, ConsensusError> accept_timeout_certificate(const TimeoutCertificate& certificate);
        std::expected<std::optional<FinalizedCheckpoint>, ConsensusError> accept_certificate(
            const QuorumCertificate& certificate);
        /// Finalize checkpoints that were held back earlier because their payload or
        /// a lower height was still missing. Returns them oldest first; empty when
        /// there is nothing left to catch up on.
        std::vector<FinalizedCheckpoint> resume_deferred_finalization();

        /// True once a quorum has certified this header — voting on it is settled.
        [[nodiscard]] bool certified(const std::string& header_hash) const;
        [[nodiscard]] bool verify_certificate(const QuorumCertificate& certificate) const;
        [[nodiscard]] bool verify_timeout_certificate(const TimeoutCertificate& certificate) const;
        [[nodiscard]] bool verify_finality_proof(const FinalityProof& proof) const;
        [[nodiscard]] std::expected<std::vector<FinalityProof>, ConsensusError> finality_proofs_after(
            std::uint64_t height,
            std::size_t   limit) const;
        [[nodiscard]] std::expected<std::optional<FinalityProof>, ConsensusError> finality_proof_for_section(
            std::uint64_t section) const;
        [[nodiscard]] std::expected<std::optional<TransactionInclusionProofV1>, ConsensusError>
                           transaction_inclusion_proof(std::string_view transaction_hash) const;
        [[nodiscard]] bool verify_transaction_inclusion_proof(const TransactionInclusionProofV1& proof) const;
        [[nodiscard]] QuorumCertificate                       genesis_certificate() const;
        [[nodiscard]] const ValidatorSetView&                 validators() const noexcept;
        [[nodiscard]] const SafetyState&                      safety_state() const noexcept;
        [[nodiscard]] const std::optional<ValidatorIdentity>& identity() const noexcept;
        [[nodiscard]] const std::optional<EpochBootstrapV1>&  epoch_bootstrap() const noexcept;
        [[nodiscard]] bool                    is_local_leader(std::uint64_t height, std::uint64_t round) const;
        [[nodiscard]] std::optional<Proposal> proposal_for(std::string_view header_hash) const;
        [[nodiscard]] std::optional<SectionBatchData> batch_for(std::string_view header_hash) const;
        [[nodiscard]] ConsensusMetricsSnapshot        metrics() const noexcept;

    private:
        [[nodiscard]] bool        verify_proposal(const Proposal& proposal) const;
        [[nodiscard]] bool        verify_vote(const Vote& vote) const;
        [[nodiscard]] bool        verify_timeout_vote(const TimeoutVote& vote) const;
        [[nodiscard]] bool        safe_to_vote(const Proposal& proposal) const;
        [[nodiscard]] static bool newer(const QuorumCertificate& left, const QuorumCertificate& right) noexcept;
        [[nodiscard]] std::optional<FinalizedCheckpoint> finalization_for(
            const QuorumCertificate& certificate) const;
        [[nodiscard]] std::optional<FinalityProof> finality_proof_for(const QuorumCertificate& certificate) const;
        std::expected<void, ConsensusError>        stage_batch_unlocked(SectionBatchData batch, bool persist);
        void                                       prune_memory(std::uint64_t finalized_height);

        ValidatorSetView                                          validators_;
        std::optional<ValidatorIdentity>                          identity_;
        std::unique_ptr<SafetyStore>                              store_;
        ProposalValidator                                         proposal_validator_;
        std::optional<EpochBootstrapV1>                           epoch_bootstrap_;
        SafetyState                                               safety_state_;
        std::map<std::string, Proposal>                           proposals_;
        std::map<std::string, SectionBatchData>                   batches_;
        std::map<std::string, QuorumCertificate>                  certificates_;
        std::map<std::string, std::map<std::string, Vote>>        votes_;
        std::map<std::string, Vote>                               observed_vote_slots_;
        std::map<std::string, std::map<std::string, TimeoutVote>> timeout_votes_;
        std::map<std::string, TimeoutVote>                        observed_timeout_slots_;
        std::map<std::string, TimeoutCertificate>                 timeout_certificates_;
        std::map<std::uint64_t, FinalityProof>                    finality_proofs_;
        std::unordered_set<std::string>                           certified_headers_;
        mutable std::recursive_mutex                              mutex_;
        bool                                                      initialized_ = false;
        std::atomic<std::uint64_t>                                proposals_created_ { 0 };
        std::atomic<std::uint64_t>                                batches_staged_ { 0 };
        std::atomic<std::uint64_t>                                votes_created_ { 0 };
        std::atomic<std::uint64_t>                                timeout_votes_created_ { 0 };
        std::atomic<std::uint64_t>                                certificates_accepted_ { 0 };
        std::atomic<std::uint64_t>                                checkpoints_finalized_ { 0 };
        std::atomic<std::uint64_t>                                stage_nanoseconds_ { 0 };
    };

} // namespace ExtraChain::Consensus
