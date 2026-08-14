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
        std::expected<void, ConsensusError> persist_state(const SafetyState& state);
        std::expected<void, ConsensusError> persist_proposal(const Proposal& proposal);
        std::expected<void, ConsensusError> persist_certificate_state(const QuorumCertificate& certificate,
                                                                      const SafetyState&       state);
        std::expected<std::vector<Proposal>, ConsensusError>          load_proposals(std::uint64_t minimum_height);
        std::expected<std::vector<QuorumCertificate>, ConsensusError> load_certificates(
            std::uint64_t minimum_height);

    private:
        [[nodiscard]] static std::string    vote_key(const Vote& vote);
        std::expected<void, ConsensusError> persist_state_unlocked(const SafetyState& state);

        std::filesystem::path        database_path_;
        std::unique_ptr<DbConnector> database_;
        std::mutex                   mutex_;
    };

} // namespace ExtraChain::Consensus
