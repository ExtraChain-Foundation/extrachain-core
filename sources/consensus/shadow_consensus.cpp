/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/shadow_consensus.h"

#include "utils/file_io.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view ValidatorSetFile  = "validator-set.msgpack";
        constexpr std::string_view IdentityFile      = "identity.msgpack";
        constexpr std::string_view SafetyFile        = "safety.sqlite";
        constexpr std::string_view ConfigurationFile = "shadow-config.msgpack";

        template <typename T>
        std::expected<T, ConsensusError> read_document(const std::filesystem::path& path) {
            const auto data = FileIo::read_all(path);
            if (!data.has_value()) {
                return std::unexpected(ConsensusError::StorageUnavailable);
            }
            const auto value = MessagePack::deserialize<T>(data.value());
            if (!value.has_value()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            return value.value();
        }

        template <typename T>
        std::expected<void, ConsensusError> write_document(const std::filesystem::path& path, const T& value) {
            const auto result = FileIo::write_atomic(path, MessagePack::serialize(value));
            if (!result.has_value()) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
            return {};
        }
    } // namespace

    ShadowConsensus::ShadowConsensus(std::unique_ptr<ConsensusEngine> engine, ShadowConfiguration configuration)
        : engine_(std::move(engine))
        , configuration_(configuration) {
    }

    std::expected<std::unique_ptr<ShadowConsensus>, ConsensusError> ShadowConsensus::load(
        const std::filesystem::path&       directory,
        const ActorId&                     network_id,
        ConsensusEngine::ProposalValidator proposal_validator) {
        const auto validator_document = read_document<ValidatorSet>(directory / ValidatorSetFile);
        if (!validator_document.has_value()) {
            return std::unexpected(validator_document.error());
        }
        if (validator_document.value().network_id != network_id) {
            return std::unexpected(ConsensusError::InvalidNetwork);
        }
        auto validators = ValidatorSetView::create(validator_document.value());
        if (!validators.has_value()) {
            return std::unexpected(validators.error());
        }

        std::optional<ValidatorIdentity> identity;
        if (std::filesystem::exists(directory / IdentityFile)) {
#ifndef _WIN32
            std::error_code permission_error;
            const auto      permissions =
                std::filesystem::status(directory / IdentityFile, permission_error).permissions();
            constexpr auto public_permissions =
                std::filesystem::perms::group_all | std::filesystem::perms::others_all;
            if (permission_error || (permissions & public_permissions) != std::filesystem::perms::none) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
#endif
            const auto document = read_document<IdentityDocument>(directory / IdentityFile);
            if (!document.has_value()) {
                return std::unexpected(document.error());
            }
            if (document.value().protocol_version != ProtocolVersion
                || document.value().validator_id != validator_id_for(document.value().key.public_key())) {
                return std::unexpected(ConsensusError::InvalidValidator);
            }
            identity =
                ValidatorIdentity { .validator_id = document.value().validator_id, .key = document.value().key };
        }

        ShadowConfiguration configuration;
        if (std::filesystem::exists(directory / ConfigurationFile)) {
            const auto loaded_configuration = read_document<ShadowConfiguration>(directory / ConfigurationFile);
            if (!loaded_configuration.has_value()
                || loaded_configuration.value().protocol_version != ProtocolVersion
                || (loaded_configuration.value().mode != ShadowMode::Observe
                    && loaded_configuration.value().mode != ShadowMode::Finality)
                || loaded_configuration.value().proposal_timeout_ms == 0
                || loaded_configuration.value().maximum_timeout_ms
                       < loaded_configuration.value().proposal_timeout_ms
                || loaded_configuration.value().maximum_batch_bytes == 0
                || loaded_configuration.value().maximum_batch_bytes > MaximumShadowBatchBytes
                || loaded_configuration.value().activation_dag_section % ShadowSectionInterval != 0
                || (loaded_configuration.value().mode == ShadowMode::Finality
                    && loaded_configuration.value().activation_dag_section == 0)) {
                return std::unexpected(ConsensusError::InvalidProtocol);
            }
            configuration = loaded_configuration.value();
        }

        auto       engine      = std::make_unique<ConsensusEngine>(std::move(validators.value()),
                                                        std::move(identity),
                                                        std::make_unique<SafetyStore>(directory / SafetyFile),
                                                        std::move(proposal_validator));
        const auto initialized = engine->initialize();
        if (!initialized.has_value()) {
            return std::unexpected(initialized.error());
        }
        return std::unique_ptr<ShadowConsensus>(new ShadowConsensus(std::move(engine), configuration));
    }

    std::expected<void, ConsensusError> ShadowConsensus::write_identity(const std::filesystem::path& directory,
                                                                        const IdentityDocument&      identity) {
        if (identity.protocol_version != ProtocolVersion || identity.key.empty()
            || identity.validator_id != validator_id_for(identity.key.public_key())) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto identity_path = directory / IdentityFile;
        const auto written       = write_document(identity_path, identity);
        if (!written.has_value()) {
            return written;
        }
#ifndef _WIN32
        std::filesystem::permissions(identity_path,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     error);
        if (error) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
#endif
        return {};
    }

    std::expected<void, ConsensusError> ShadowConsensus::write_validator_set(
        const std::filesystem::path& directory,
        const ValidatorSet&          validators) {
        if (!ValidatorSetView::create(validators).has_value()) {
            return std::unexpected(ConsensusError::InvalidValidatorSet);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        return write_document(directory / ValidatorSetFile, validators);
    }

    std::expected<void, ConsensusError> ShadowConsensus::write_configuration(
        const std::filesystem::path& directory,
        const ShadowConfiguration&   configuration) {
        if (configuration.protocol_version != ProtocolVersion
            || (configuration.mode != ShadowMode::Observe && configuration.mode != ShadowMode::Finality)
            || configuration.proposal_timeout_ms == 0
            || configuration.maximum_timeout_ms < configuration.proposal_timeout_ms
            || configuration.maximum_batch_bytes == 0
            || configuration.maximum_batch_bytes > MaximumShadowBatchBytes
            || configuration.activation_dag_section % ShadowSectionInterval != 0
            || (configuration.mode == ShadowMode::Finality && configuration.activation_dag_section == 0)) {
            return std::unexpected(ConsensusError::InvalidProtocol);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        return write_document(directory / ConfigurationFile, configuration);
    }

    std::expected<std::optional<Proposal>, ConsensusError> ShadowConsensus::make_checkpoint_proposal(
        ShadowCheckpoint checkpoint,
        std::uint64_t    round) {
        if (!engine_->identity().has_value()) {
            return std::optional<Proposal> {};
        }
        const auto height = engine_->safety_state().highest_certificate.value().height + 1;
        if (!engine_->is_local_leader(height, round)) {
            return std::optional<Proposal> {};
        }
        auto proposal =
            engine_->make_proposal(std::move(checkpoint.batch), std::move(checkpoint.section_root), round);
        if (!proposal.has_value()) {
            return std::unexpected(proposal.error());
        }
        return std::optional<Proposal>(std::move(proposal.value()));
    }

    std::expected<std::optional<Vote>, ConsensusError> ShadowConsensus::receive_proposal(
        const Proposal&  proposal,
        std::string_view peer_identifier) {
        if (!peer_matches_validator(proposal.proposer_id, peer_identifier)) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        if (!engine_->identity().has_value()) {
            const auto observed = engine_->observe_proposal(proposal);
            if (!observed.has_value()) {
                return std::unexpected(observed.error());
            }
            return std::optional<Vote> {};
        }
        auto vote = engine_->accept_proposal(proposal);
        if (!vote.has_value()) {
            return std::unexpected(vote.error());
        }
        return std::optional<Vote>(std::move(vote.value()));
    }

    std::expected<VoteAcceptance, ConsensusError> ShadowConsensus::receive_vote(const Vote&      vote,
                                                                                std::string_view peer_identifier) {
        if (!peer_matches_validator(vote.validator_id, peer_identifier)) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        return engine_->accept_vote(vote);
    }

    std::expected<TimeoutVote, ConsensusError> ShadowConsensus::make_timeout_vote(std::uint64_t height,
                                                                                  std::uint64_t round) {
        return engine_->make_timeout_vote(height, round);
    }

    std::expected<TimeoutAcceptance, ConsensusError> ShadowConsensus::receive_timeout_vote(
        const TimeoutVote& vote,
        std::string_view   peer_identifier) {
        if (!peer_matches_validator(vote.validator_id, peer_identifier)) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        return engine_->accept_timeout_vote(vote);
    }

    std::expected<void, ConsensusError> ShadowConsensus::receive_timeout_certificate(
        const TimeoutCertificate& certificate) {
        return engine_->accept_timeout_certificate(certificate);
    }

    std::expected<std::optional<FinalizedCheckpoint>, ConsensusError> ShadowConsensus::receive_certificate(
        const QuorumCertificate& certificate) {
        return engine_->accept_certificate(certificate);
    }

    const ConsensusEngine& ShadowConsensus::engine() const noexcept {
        return *engine_;
    }

    ConsensusEngine& ShadowConsensus::engine() noexcept {
        return *engine_;
    }

    const ShadowConfiguration& ShadowConsensus::configuration() const noexcept {
        return configuration_;
    }

    bool ShadowConsensus::peer_matches_validator(std::string_view validator_id,
                                                 std::string_view peer_identifier) const {
        const auto* validator = engine_->validators().find(validator_id);
        return validator != nullptr && validator->node_identifier == peer_identifier;
    }

} // namespace ExtraChain::Consensus
