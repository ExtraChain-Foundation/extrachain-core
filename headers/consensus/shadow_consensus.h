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
#include <optional>
#include <string_view>

#include "consensus/consensus_engine.h"

namespace ExtraChain::Consensus {

    struct IdentityDocument {
        std::uint16_t protocol_version = ProtocolVersion;
        std::string   validator_id;
        KeyPrivate    key;

        MSGPACK_DEFINE(protocol_version, validator_id, key)
    };

    struct ShadowCheckpoint {
        std::uint64_t dag_section = 0;
        std::string   section_root;
        std::string   transaction_root;
    };

    class EXTRACHAIN_EXPORT ShadowConsensus {
    public:
        static std::expected<std::unique_ptr<ShadowConsensus>, ConsensusError> load(
            const std::filesystem::path&       directory,
            const ActorId&                     network_id,
            ConsensusEngine::ProposalValidator proposal_validator = {});

        static std::expected<void, ConsensusError> write_identity(const std::filesystem::path& directory,
                                                                  const IdentityDocument&      identity);
        static std::expected<void, ConsensusError> write_validator_set(const std::filesystem::path& directory,
                                                                       const ValidatorSet&          validators);

        std::expected<std::optional<Proposal>, ConsensusError> make_checkpoint_proposal(
            ShadowCheckpoint checkpoint,
            std::uint64_t    round = 0);
        std::expected<std::optional<Vote>, ConsensusError> receive_proposal(const Proposal&  proposal,
                                                                            std::string_view peer_identifier);
        std::expected<VoteAcceptance, ConsensusError>      receive_vote(const Vote&      vote,
                                                                        std::string_view peer_identifier);
        std::expected<std::optional<FinalizedCheckpoint>, ConsensusError> receive_certificate(
            const QuorumCertificate& certificate);

        [[nodiscard]] const ConsensusEngine& engine() const noexcept;
        [[nodiscard]] ConsensusEngine&       engine() noexcept;

    private:
        explicit ShadowConsensus(std::unique_ptr<ConsensusEngine> engine);
        [[nodiscard]] bool peer_matches_validator(std::string_view validator_id,
                                                  std::string_view peer_identifier) const;

        std::unique_ptr<ConsensusEngine> engine_;
    };

} // namespace ExtraChain::Consensus
