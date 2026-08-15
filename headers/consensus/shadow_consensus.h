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
#include <vector>

#include "consensus/consensus_engine.h"

namespace ExtraChain::Consensus {

    struct IdentityDocument {
        std::uint16_t protocol_version = ProtocolVersion;
        std::string   validator_id;
        KeyPrivate    key;

        MSGPACK_DEFINE(protocol_version, validator_id, key)
    };

    struct ShadowCheckpoint {
        SectionBatchManifest batch;
        StateCommitmentV2    state;
    };

    struct PendingRecoveryV1 {
        RecoveryDocumentV2 document;
        ValidatorSet       next_validators;
        std::uint64_t      first_seen_ms = 0;

        MSGPACK_DEFINE(document, next_validators, first_seen_ms)
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
        static std::expected<void, ConsensusError> write_configuration(const std::filesystem::path& directory,
                                                                       const ShadowConfiguration&   configuration);
        static std::expected<void, ConsensusError> write_governance_policy(const std::filesystem::path& directory,
                                                                           const MultisigPolicy&        policy);
        static std::expected<void, ConsensusError> write_recovery_policy(const std::filesystem::path& directory,
                                                                         const MultisigPolicy&        policy);
        static std::expected<void, ConsensusError> write_trust_anchor(const std::filesystem::path& directory,
                                                                      const TrustAnchorV1&         anchor);
        static std::expected<void, ConsensusError> write_activation_manifest(
            const std::filesystem::path& directory,
            const ActivationManifestV1&  manifest,
            const MultisigPolicy&        policy);
        static std::expected<void, ConsensusError> write_epoch_identity(const std::filesystem::path& directory,
                                                                        std::uint64_t                epoch,
                                                                        const IdentityDocument&      identity);

        std::expected<std::optional<Proposal>, ConsensusError> make_checkpoint_proposal(
            ShadowCheckpoint checkpoint,
            std::uint64_t    round = 0);
        std::expected<std::optional<Vote>, ConsensusError> receive_proposal(const Proposal&  proposal,
                                                                            std::string_view peer_identifier);
        std::expected<VoteAcceptance, ConsensusError>      receive_vote(const Vote&      vote,
                                                                        std::string_view peer_identifier);
        std::expected<TimeoutVote, ConsensusError> make_timeout_vote(std::uint64_t height, std::uint64_t round);
        std::expected<TimeoutAcceptance, ConsensusError> receive_timeout_vote(const TimeoutVote& vote,
                                                                              std::string_view   peer_identifier);
        std::expected<void, ConsensusError> receive_timeout_certificate(const TimeoutCertificate& certificate);
        std::expected<std::optional<FinalizedCheckpoint>, ConsensusError> receive_certificate(
            const QuorumCertificate& certificate);
        std::expected<void, ConsensusError>               schedule_epoch(const EpochChangeV1&               change,
                                                                         ValidatorSet                       next_validators,
                                                                         const TransactionInclusionProofV1& proof);
        [[nodiscard]] std::expected<void, ConsensusError> validate_epoch_request(
            const EpochChangeRequestV1& request,
            std::uint64_t               proposal_height) const;
        std::expected<bool, ConsensusError> activate_scheduled_epoch();
        std::expected<void, ConsensusError> schedule_recovery(const RecoveryDocumentV2& recovery,
                                                              ValidatorSet              next_validators,
                                                              std::uint64_t             now_ms);
        std::expected<bool, ConsensusError> activate_scheduled_recovery(std::uint64_t now_ms);

        [[nodiscard]] const ConsensusEngine&                  engine() const noexcept;
        [[nodiscard]] ConsensusEngine&                        engine() noexcept;
        [[nodiscard]] const ShadowConfiguration&              configuration() const noexcept;
        [[nodiscard]] const std::optional<EpochTransitionV1>& pending_epoch() const noexcept;
        [[nodiscard]] const std::optional<PendingRecoveryV1>& pending_recovery() const noexcept;
        [[nodiscard]] std::vector<EpochStartV1>               epoch_starts() const;
        [[nodiscard]] const std::optional<TrustAnchorV1>&     trust_anchor() const noexcept;

    private:
        explicit ShadowConsensus(std::filesystem::path              directory,
                                 std::unique_ptr<ConsensusEngine>   engine,
                                 ShadowConfiguration                configuration,
                                 std::optional<MultisigPolicy>      governance_policy,
                                 std::vector<EpochStartV1>          epoch_history,
                                 std::optional<EpochTransitionV1>   pending_epoch,
                                 std::optional<EpochBootstrapV1>    active_bootstrap,
                                 std::uint64_t                      minimum_governance_sequence,
                                 std::uint64_t                      minimum_recovery_sequence,
                                 ConsensusEngine::ProposalValidator proposal_validator);
        [[nodiscard]] bool peer_matches_validator(std::string_view validator_id,
                                                  std::string_view peer_identifier) const;

        std::unique_ptr<ConsensusEngine>   engine_;
        ShadowConfiguration                configuration_;
        std::filesystem::path              directory_;
        std::optional<MultisigPolicy>      governance_policy_;
        std::optional<MultisigPolicy>      recovery_policy_;
        std::optional<TrustAnchorV1>       trust_anchor_;
        std::vector<EpochStartV1>          epoch_history_;
        std::optional<EpochTransitionV1>   pending_epoch_;
        std::optional<EpochBootstrapV1>    active_bootstrap_;
        std::optional<PendingRecoveryV1>   pending_recovery_;
        std::uint64_t                      minimum_governance_sequence_ = 1;
        std::uint64_t                      minimum_recovery_sequence_   = 1;
        ConsensusEngine::ProposalValidator proposal_validator_;
    };

} // namespace ExtraChain::Consensus
