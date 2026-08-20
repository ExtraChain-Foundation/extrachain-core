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
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "consensus/consensus_types.h"

class DbConnector;

namespace ExtraChain::Consensus {

    class EXTRACHAIN_EXPORT SafetyStore {
    public:
        explicit SafetyStore(std::filesystem::path database_path);
        ~SafetyStore();

        SafetyStore(const SafetyStore&)            = delete;
        SafetyStore& operator=(const SafetyStore&) = delete;

        std::expected<void, ConsensusError>                       open();
        std::expected<std::optional<SafetyState>, ConsensusError> load_state();
        std::expected<bool, ConsensusError> persist_vote(const Vote& vote, const SafetyState& state);
        std::expected<bool, ConsensusError> persist_timeout_vote(const TimeoutVote& vote,
                                                                 const SafetyState& state);
        std::expected<void, ConsensusError> persist_state(const SafetyState& state);
        std::expected<void, ConsensusError> persist_proposal(const Proposal& proposal);
        std::expected<void, ConsensusError> persist_batch(const SectionBatchData& batch, std::uint64_t height);
        std::expected<void, ConsensusError> persist_proposal_batch(const Proposal&         proposal,
                                                                   const SectionBatchData& batch);
        std::expected<bool, ConsensusError> persist_proposal_batch_vote(const Proposal&         proposal,
                                                                        const SectionBatchData& batch,
                                                                        const Vote&             vote,
                                                                        const SafetyState&      state);
        std::expected<void, ConsensusError> persist_certificate_state(const QuorumCertificate& certificate,
                                                                      const Proposal&          proposal,
                                                                      const SafetyState&       state,
                                                                      const std::optional<FinalityProof>& proof);
        std::expected<void, ConsensusError> persist_timeout_certificate_state(
            const TimeoutCertificate& certificate,
            const SafetyState&        state);
        std::expected<std::vector<Proposal>, ConsensusError> load_proposals(std::uint64_t minimum_height);
        std::expected<std::optional<SectionBatchData>, ConsensusError> load_batch(std::string_view header_hash);
        std::expected<std::vector<QuorumCertificate>, ConsensusError>  load_certificates(
             std::uint64_t minimum_height);
        std::expected<std::vector<TimeoutCertificate>, ConsensusError> load_timeout_certificates(
            std::uint64_t minimum_height);
        std::expected<std::vector<FinalityProof>, ConsensusError> load_finality_proofs_after(std::uint64_t height,
                                                                                             std::size_t   limit);
        std::expected<std::optional<FinalityProof>, ConsensusError> load_finality_proof_for_section(
            std::uint64_t section);

    private:
        [[nodiscard]] static std::string    vote_key(const Vote& vote);
        [[nodiscard]] static std::string    timeout_vote_key(const TimeoutVote& vote);
        std::expected<void, ConsensusError> persist_state_unlocked(const SafetyState& state);

        std::filesystem::path        database_path_;
        std::unique_ptr<DbConnector> database_;
        std::mutex                   mutex_;
    };

} // namespace ExtraChain::Consensus
