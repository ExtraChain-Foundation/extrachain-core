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

#include <fmt/format.h>

#include "consensus/light_client.h"
#include "utils/file_io.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view ValidatorSetFile       = "validator-set.msgpack";
        constexpr std::string_view IdentityFile           = "identity.msgpack";
        constexpr std::string_view SafetyFile             = "safety.sqlite";
        constexpr std::string_view ConfigurationFile      = "shadow-config.msgpack";
        constexpr std::string_view GovernancePolicyFile   = "governance-policy.msgpack";
        constexpr std::string_view ActivationManifestFile = "activation-manifest.msgpack";
        constexpr std::string_view EpochHistoryFile       = "epoch-history.msgpack";
        constexpr std::string_view PendingEpochFile       = "pending-epoch.msgpack";

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

        std::filesystem::path epoch_identity_path(const std::filesystem::path& directory, std::uint64_t epoch) {
            return directory / fmt::format("identity-epoch-{}.msgpack", epoch);
        }

        std::filesystem::path epoch_safety_path(const std::filesystem::path& directory, std::uint64_t epoch) {
            return directory / fmt::format("safety-epoch-{}.sqlite", epoch);
        }

        std::expected<std::optional<ValidatorIdentity>, ConsensusError> read_identity(
            const std::filesystem::path& path,
            const ValidatorSetView&      validators) {
            if (!std::filesystem::exists(path)) {
                return std::optional<ValidatorIdentity> {};
            }
#ifndef _WIN32
            std::error_code permission_error;
            const auto      permissions = std::filesystem::status(path, permission_error).permissions();
            constexpr auto  public_permissions =
                std::filesystem::perms::group_all | std::filesystem::perms::others_all;
            if (permission_error || (permissions & public_permissions) != std::filesystem::perms::none) {
                return std::unexpected(ConsensusError::StorageFailure);
            }
#endif
            const auto document = read_document<IdentityDocument>(path);
            if (!document.has_value() || document.value().protocol_version != ProtocolVersion
                || document.value().validator_id != validator_id_for(document.value().key.public_key())) {
                return std::unexpected(ConsensusError::InvalidValidator);
            }
            const auto* validator = validators.find(document.value().validator_id);
            if (validator == nullptr) {
                return std::optional<ValidatorIdentity> {};
            }
            return std::optional<ValidatorIdentity>(ValidatorIdentity {
                .validator_id = document.value().validator_id,
                .key          = document.value().key,
            });
        }
    } // namespace

    ShadowConsensus::ShadowConsensus(std::filesystem::path              directory,
                                     std::unique_ptr<ConsensusEngine>   engine,
                                     ShadowConfiguration                configuration,
                                     std::optional<MultisigPolicy>      governance_policy,
                                     std::vector<EpochTransitionV1>     epoch_history,
                                     std::optional<EpochTransitionV1>   pending_epoch,
                                     std::optional<EpochBootstrapV1>    active_bootstrap,
                                     std::uint64_t                      minimum_governance_sequence,
                                     ConsensusEngine::ProposalValidator proposal_validator)
        : engine_(std::move(engine))
        , configuration_(configuration)
        , directory_(std::move(directory))
        , governance_policy_(std::move(governance_policy))
        , epoch_history_(std::move(epoch_history))
        , pending_epoch_(std::move(pending_epoch))
        , active_bootstrap_(std::move(active_bootstrap))
        , minimum_governance_sequence_(minimum_governance_sequence)
        , proposal_validator_(std::move(proposal_validator)) {
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
        auto initial_validators = ValidatorSetView::create(validator_document.value());
        if (!initial_validators.has_value()) {
            return std::unexpected(initial_validators.error());
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

        std::optional<MultisigPolicy> governance_policy;
        std::uint64_t                 minimum_sequence = 1;
        if (configuration.mode == ShadowMode::Finality) {
            const auto policy   = read_document<MultisigPolicy>(directory / GovernancePolicyFile);
            const auto manifest = read_document<ActivationManifestV1>(directory / ActivationManifestFile);
            if (initial_validators.value().active().size() != ShadowCommitteeSize || !policy.has_value()
                || !manifest.has_value() || manifest.value().activation_height == 0
                || manifest.value().authorization.sequence == std::numeric_limits<std::uint64_t>::max()
                || !verify_multisig_policy(policy.value())
                || !verify_activation_manifest(manifest.value(),
                                               policy.value(),
                                               manifest.value().activation_height - 1,
                                               1)
                || manifest.value().network_id != network_id
                || manifest.value().activation_height != configuration.activation_height
                || manifest.value().activation_dag_section != configuration.activation_dag_section
                || manifest.value().validator_set_hash != hash_validator_set(validator_document.value())) {
                return std::unexpected(ConsensusError::InvalidGovernance);
            }
            governance_policy = policy.value();
            minimum_sequence  = manifest.value().authorization.sequence + 1;
        }

        std::vector<EpochTransitionV1> epoch_history;
        if (std::filesystem::exists(directory / EpochHistoryFile)) {
            const auto loaded = read_document<std::vector<EpochTransitionV1>>(directory / EpochHistoryFile);
            if (!loaded.has_value() || !governance_policy.has_value()) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            epoch_history = loaded.value();
        }

        ValidatorSet                    active_document = validator_document.value();
        std::optional<EpochBootstrapV1> active_bootstrap;
        for (const auto& transition : epoch_history) {
            auto verifier = LightClientVerifier::create(active_document, active_bootstrap);
            if (!verifier.has_value() || transition.protocol_version != ProtocolVersion
                || transition.change.authorization.sequence == std::numeric_limits<std::uint64_t>::max()
                || !verifier.value()
                        .schedule_epoch(transition.change,
                                        transition.next_validators,
                                        governance_policy.value(),
                                        transition.proof,
                                        minimum_sequence)
                        .has_value()) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            const auto expected =
                make_epoch_bootstrap(transition.change, transition.next_validators, transition.proof);
            if (!expected.has_value()
                || hash_epoch_bootstrap(expected.value()) != hash_epoch_bootstrap(transition.bootstrap)) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            minimum_sequence = transition.change.authorization.sequence + 1;
            active_document  = transition.next_validators;
            active_bootstrap = transition.bootstrap;
        }

        auto validators = ValidatorSetView::create(active_document);
        if (!validators.has_value()) {
            return std::unexpected(validators.error());
        }

        std::optional<EpochTransitionV1> pending_epoch;
        if (std::filesystem::exists(directory / PendingEpochFile)) {
            const auto loaded = read_document<EpochTransitionV1>(directory / PendingEpochFile);
            if (!loaded.has_value()) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            const bool already_active = !epoch_history.empty()
                                        && hash_epoch_bootstrap(epoch_history.back().bootstrap)
                                               == hash_epoch_bootstrap(loaded.value().bootstrap);
            if (already_active) {
                std::error_code remove_error;
                std::filesystem::remove(directory / PendingEpochFile, remove_error);
                if (remove_error) {
                    return std::unexpected(ConsensusError::StorageFailure);
                }
            } else {
                auto verifier = LightClientVerifier::create(active_document, active_bootstrap);
                if (!governance_policy.has_value() || !verifier.has_value()
                    || loaded.value().change.authorization.sequence == std::numeric_limits<std::uint64_t>::max()
                    || !verifier.value()
                            .schedule_epoch(loaded.value().change,
                                            loaded.value().next_validators,
                                            governance_policy.value(),
                                            loaded.value().proof,
                                            minimum_sequence)
                            .has_value()) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                const auto expected = make_epoch_bootstrap(loaded.value().change,
                                                           loaded.value().next_validators,
                                                           loaded.value().proof);
                if (!expected.has_value()
                    || hash_epoch_bootstrap(expected.value()) != hash_epoch_bootstrap(loaded.value().bootstrap)) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                pending_epoch = loaded.value();
            }
        }

        auto       identity_path = directory / IdentityFile;
        const auto epoch_path    = epoch_identity_path(directory, validators.value().document().epoch);
        if (active_bootstrap.has_value() && std::filesystem::exists(epoch_path)) {
            identity_path = epoch_path;
        }
        auto identity = read_identity(identity_path, validators.value());
        if (!identity.has_value()) {
            return std::unexpected(identity.error());
        }

        const auto safety_path = active_bootstrap.has_value()
                                     ? epoch_safety_path(directory, validators.value().document().epoch)
                                     : directory / SafetyFile;
        auto       engine      = std::make_unique<ConsensusEngine>(std::move(validators.value()),
                                                        std::move(identity.value()),
                                                        std::make_unique<SafetyStore>(safety_path),
                                                        proposal_validator,
                                                        active_bootstrap);
        const auto initialized = engine->initialize();
        if (!initialized.has_value()) {
            return std::unexpected(initialized.error());
        }
        return std::unique_ptr<ShadowConsensus>(new ShadowConsensus(directory,
                                                                    std::move(engine),
                                                                    configuration,
                                                                    std::move(governance_policy),
                                                                    std::move(epoch_history),
                                                                    std::move(pending_epoch),
                                                                    std::move(active_bootstrap),
                                                                    minimum_sequence,
                                                                    std::move(proposal_validator)));
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

    std::expected<void, ConsensusError> ShadowConsensus::write_epoch_identity(
        const std::filesystem::path& directory,
        std::uint64_t                epoch,
        const IdentityDocument&      identity) {
        if (epoch == 0 || identity.protocol_version != ProtocolVersion || identity.key.empty()
            || identity.validator_id != validator_id_for(identity.key.public_key())) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        const auto path    = epoch_identity_path(directory, epoch);
        const auto written = write_document(path, identity);
        if (!written.has_value()) {
            return written;
        }
#ifndef _WIN32
        std::filesystem::permissions(path,
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

    std::expected<void, ConsensusError> ShadowConsensus::write_governance_policy(
        const std::filesystem::path& directory,
        const MultisigPolicy&        policy) {
        if (!verify_multisig_policy(policy) || policy.public_keys.size() != GovernanceSignerCount
            || policy.threshold != GovernanceThreshold) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        return write_document(directory / GovernancePolicyFile, policy);
    }

    std::expected<void, ConsensusError> ShadowConsensus::write_activation_manifest(
        const std::filesystem::path& directory,
        const ActivationManifestV1&  manifest,
        const MultisigPolicy&        policy) {
        if (manifest.activation_height == 0
            || !verify_activation_manifest(manifest, policy, manifest.activation_height - 1, 1)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        return write_document(directory / ActivationManifestFile, manifest);
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

    std::expected<void, ConsensusError> ShadowConsensus::schedule_epoch(const EpochChangeV1& change,
                                                                        ValidatorSet         next_validators,
                                                                        const TransactionInclusionProofV1& proof) {
        if (configuration_.mode != ShadowMode::Finality || !governance_policy_.has_value()
            || pending_epoch_.has_value() || change.current_epoch != engine_->validators().document().epoch
            || change.current_validator_set_hash != engine_->validators().hash()
            || !engine_->verify_transaction_inclusion_proof(proof)) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        const auto next      = ValidatorSetView::create_epoch_transition(next_validators,
                                                                    change,
                                                                    governance_policy_.value(),
                                                                    proof.height,
                                                                    minimum_governance_sequence_);
        const auto bootstrap = make_epoch_bootstrap(change, next_validators, proof);
        if (!next.has_value() || !bootstrap.has_value()) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        const auto& highest = engine_->safety_state().highest_certificate;
        if (!highest.has_value() || highest.value().height + 1 > bootstrap.value().activation_height) {
            return std::unexpected(ConsensusError::InvalidHeight);
        }
        EpochTransitionV1 transition {
            .change          = change,
            .next_validators = std::move(next_validators),
            .proof           = proof,
            .bootstrap       = bootstrap.value(),
        };
        const auto written = write_document(directory_ / PendingEpochFile, transition);
        if (!written.has_value()) {
            return std::unexpected(written.error());
        }
        pending_epoch_ = std::move(transition);
        return {};
    }

    std::expected<void, ConsensusError> ShadowConsensus::validate_epoch_request(
        const EpochChangeRequestV1& request,
        std::uint64_t               proposal_height) const {
        if (configuration_.mode != ShadowMode::Finality || !governance_policy_.has_value()
            || pending_epoch_.has_value() || request.protocol_version != ProtocolVersion
            || request.change.authorization.sequence == std::numeric_limits<std::uint64_t>::max()
            || request.change.current_epoch != engine_->validators().document().epoch
            || request.change.current_validator_set_hash != engine_->validators().hash()
            || request.change.activation_height != proposal_height + 3) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        const auto validators = ValidatorSetView::create_epoch_transition(request.next_validators,
                                                                          request.change,
                                                                          governance_policy_.value(),
                                                                          proposal_height,
                                                                          minimum_governance_sequence_);
        return validators.has_value() ? std::expected<void, ConsensusError> {}
                                      : std::unexpected(validators.error());
    }

    std::expected<bool, ConsensusError> ShadowConsensus::activate_scheduled_epoch() {
        if (!pending_epoch_.has_value()) {
            return false;
        }
        const auto& transition = pending_epoch_.value();
        const auto& highest    = engine_->safety_state().highest_certificate;
        if (!highest.has_value() || highest.value().height + 1 < transition.bootstrap.activation_height) {
            return false;
        }
        if (highest.value().height + 1 != transition.bootstrap.activation_height
            || hash_certificate(highest.value()) != transition.bootstrap.previous_decision_certificate_hash
            || !governance_policy_.has_value()) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        auto validators = ValidatorSetView::create_epoch_transition(transition.next_validators,
                                                                    transition.change,
                                                                    governance_policy_.value(),
                                                                    transition.proof.height,
                                                                    minimum_governance_sequence_);
        if (!validators.has_value()) {
            return std::unexpected(validators.error());
        }

        auto identity_path = epoch_identity_path(directory_, transition.next_validators.epoch);
        if (!std::filesystem::exists(identity_path)) {
            identity_path = directory_ / IdentityFile;
        }
        auto identity = read_identity(identity_path, validators.value());
        if (!identity.has_value()) {
            return std::unexpected(identity.error());
        }

        auto next_engine =
            std::make_unique<ConsensusEngine>(std::move(validators.value()),
                                              std::move(identity.value()),
                                              std::make_unique<SafetyStore>(
                                                  epoch_safety_path(directory_, transition.next_validators.epoch)),
                                              proposal_validator_,
                                              transition.bootstrap);
        const auto initialized = next_engine->initialize();
        if (!initialized.has_value()) {
            return std::unexpected(initialized.error());
        }

        auto next_history = epoch_history_;
        next_history.push_back(transition);
        const auto history_written = write_document(directory_ / EpochHistoryFile, next_history);
        if (!history_written.has_value()) {
            return std::unexpected(history_written.error());
        }

        std::error_code remove_error;
        std::filesystem::remove(directory_ / PendingEpochFile, remove_error);
        if (remove_error) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        engine_                      = std::move(next_engine);
        epoch_history_               = std::move(next_history);
        active_bootstrap_            = transition.bootstrap;
        minimum_governance_sequence_ = transition.change.authorization.sequence + 1;
        pending_epoch_.reset();
        return true;
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

    const std::optional<EpochTransitionV1>& ShadowConsensus::pending_epoch() const noexcept {
        return pending_epoch_;
    }

    bool ShadowConsensus::peer_matches_validator(std::string_view validator_id,
                                                 std::string_view peer_identifier) const {
        const auto* validator = engine_->validators().find(validator_id);
        return validator != nullptr && validator->node_identifier == peer_identifier;
    }

} // namespace ExtraChain::Consensus
