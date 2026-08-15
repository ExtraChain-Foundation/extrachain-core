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
        constexpr std::string_view RecoveryPolicyFile     = "recovery-policy.msgpack";
        constexpr std::string_view TrustAnchorFile        = "trust-anchor.msgpack";
        constexpr std::string_view ActivationManifestFile = "activation-manifest.msgpack";
        constexpr std::string_view EpochHistoryFile       = "epoch-history.msgpack";
        constexpr std::string_view PendingEpochFile       = "pending-epoch.msgpack";
        constexpr std::string_view PendingRecoveryFile    = "pending-recovery.msgpack";

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
                                     std::vector<EpochStartV1>          epoch_history,
                                     std::optional<EpochTransitionV1>   pending_epoch,
                                     std::optional<EpochBootstrapV1>    active_bootstrap,
                                     std::uint64_t                      minimum_governance_sequence,
                                     std::uint64_t                      minimum_recovery_sequence,
                                     ConsensusEngine::ProposalValidator proposal_validator)
        : engine_(std::move(engine))
        , configuration_(configuration)
        , directory_(std::move(directory))
        , governance_policy_(std::move(governance_policy))
        , epoch_history_(std::move(epoch_history))
        , pending_epoch_(std::move(pending_epoch))
        , active_bootstrap_(std::move(active_bootstrap))
        , minimum_governance_sequence_(minimum_governance_sequence)
        , minimum_recovery_sequence_(minimum_recovery_sequence)
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
        std::optional<MultisigPolicy> recovery_policy;
        std::optional<TrustAnchorV1>  trust_anchor;
        std::uint64_t                 minimum_governance_sequence = 1;
        std::uint64_t                 minimum_recovery_sequence   = 1;
        if (configuration.mode == ShadowMode::Finality) {
            const auto policy   = read_document<MultisigPolicy>(directory / GovernancePolicyFile);
            const auto recovery = read_document<MultisigPolicy>(directory / RecoveryPolicyFile);
            const auto anchor   = read_document<TrustAnchorV1>(directory / TrustAnchorFile);
            const auto manifest = read_document<ActivationManifestV1>(directory / ActivationManifestFile);
            if (initial_validators.value().active().size() != ShadowCommitteeSize || !policy.has_value()
                || !recovery.has_value() || !anchor.has_value() || !manifest.has_value()
                || manifest.value().activation_height == 0
                || manifest.value().authorization.sequence == std::numeric_limits<std::uint64_t>::max()
                || !verify_multisig_policy(policy.value())
                || !verify_activation_manifest(manifest.value(),
                                               policy.value(),
                                               manifest.value().activation_height - 1,
                                               1)
                || manifest.value().network_id != network_id
                || manifest.value().activation_height != configuration.activation_height
                || manifest.value().activation_dag_section != configuration.activation_dag_section
                || manifest.value().validator_set_hash != hash_validator_set(validator_document.value())
                || !verify_trust_anchor(anchor.value()) || anchor.value().network_id != network_id
                || hash_validator_set(anchor.value().initial_validators)
                       != hash_validator_set(validator_document.value())
                || anchor.value().governance_policy.policy_hash != policy.value().policy_hash
                || anchor.value().recovery_policy.policy_hash != recovery.value().policy_hash) {
                return std::unexpected(ConsensusError::InvalidGovernance);
            }
            governance_policy           = policy.value();
            recovery_policy             = recovery.value();
            trust_anchor                = anchor.value();
            minimum_governance_sequence = manifest.value().authorization.sequence + 1;
        }

        std::vector<EpochStartV1> epoch_history;
        if (std::filesystem::exists(directory / EpochHistoryFile)) {
            const auto loaded = read_document<std::vector<EpochStartV1>>(directory / EpochHistoryFile);
            if (!loaded.has_value() || !governance_policy.has_value() || !recovery_policy.has_value()) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            epoch_history = loaded.value();
        }

        ValidatorSetView                active_validators = initial_validators.value();
        ValidatorSet                    active_document   = validator_document.value();
        std::optional<EpochBootstrapV1> active_bootstrap;
        for (const auto& start : epoch_history) {
            if (start.validators.epoch <= active_document.epoch
                || start.bootstrap.previous_epoch != active_document.epoch) {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            if (start.kind == EpochStartKind::Normal) {
                LightClientVerifier verifier(active_validators, active_bootstrap);
                if (!start.normal_transition.has_value()
                    || !verifier
                            .schedule_epoch(start.normal_transition.value().change,
                                            start.validators,
                                            governance_policy.value(),
                                            start.normal_transition.value().proof,
                                            minimum_governance_sequence)
                            .has_value()
                    || !verifier.pending_validators().has_value()
                    || hash_epoch_bootstrap(start.normal_transition.value().bootstrap)
                           != hash_epoch_bootstrap(start.bootstrap)) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                active_validators           = verifier.pending_validators().value();
                minimum_governance_sequence = start.normal_transition.value().change.authorization.sequence + 1;
            } else if (start.kind == EpochStartKind::Recovery && start.recovery.has_value()) {
                const auto& recovery = start.recovery.value();
                if (!verify_recovery_document(recovery,
                                              recovery_policy.value(),
                                              active_document,
                                              start.validators,
                                              minimum_recovery_sequence,
                                              start.bootstrap.previous_finalized_height,
                                              recovery.finalized_header_hash,
                                              start.bootstrap.previous_state_commitment)
                    || recovery.finalized_state_commitment != start.bootstrap.previous_state_commitment
                    || recovery.activation_epoch != start.bootstrap.epoch) {
                    return std::unexpected(ConsensusError::InvalidEpoch);
                }
                auto next = ValidatorSetView::create_recovery_transition(start.validators, recovery.operators);
                if (!next.has_value()) {
                    return std::unexpected(next.error());
                }
                active_validators         = std::move(next.value());
                minimum_recovery_sequence = recovery.recovery_sequence + 1;
            } else {
                return std::unexpected(ConsensusError::InvalidEpoch);
            }
            active_document  = start.validators;
            active_bootstrap = start.bootstrap;
        }

        std::expected<ValidatorSetView, ConsensusError> validators(std::move(active_validators));

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
                LightClientVerifier verifier(validators.value(), active_bootstrap);
                if (!governance_policy.has_value()
                    || loaded.value().change.authorization.sequence == std::numeric_limits<std::uint64_t>::max()
                    || !verifier
                            .schedule_epoch(loaded.value().change,
                                            loaded.value().next_validators,
                                            governance_policy.value(),
                                            loaded.value().proof,
                                            minimum_governance_sequence)
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
        auto instance              = std::unique_ptr<ShadowConsensus>(new ShadowConsensus(directory,
                                                                             std::move(engine),
                                                                             configuration,
                                                                             std::move(governance_policy),
                                                                             std::move(epoch_history),
                                                                             std::move(pending_epoch),
                                                                             std::move(active_bootstrap),
                                                                             minimum_governance_sequence,
                                                                             minimum_recovery_sequence,
                                                                             std::move(proposal_validator)));
        instance->recovery_policy_ = std::move(recovery_policy);
        instance->trust_anchor_    = std::move(trust_anchor);
        if (std::filesystem::exists(directory / PendingRecoveryFile)) {
            const auto pending = read_document<PendingRecoveryV1>(directory / PendingRecoveryFile);
            if (!pending.has_value() || !instance->recovery_policy_.has_value()) {
                return std::unexpected(ConsensusError::InvalidGovernance);
            }
            const bool already_active = !instance->epoch_history_.empty()
                                        && instance->epoch_history_.back().kind == EpochStartKind::Recovery
                                        && instance->epoch_history_.back().recovery.has_value()
                                        && recovery_action_hash(instance->epoch_history_.back().recovery.value())
                                               == recovery_action_hash(pending.value().document)
                                        && hash_validator_set(instance->epoch_history_.back().validators)
                                               == hash_validator_set(pending.value().next_validators);
            if (already_active) {
                std::error_code remove_error;
                std::filesystem::remove(directory / PendingRecoveryFile, remove_error);
                if (remove_error) {
                    return std::unexpected(ConsensusError::StorageFailure);
                }
                return instance;
            }
            const auto finalized_height = instance->engine_->safety_state().finalized_height;
            const auto proofs           = finalized_height == 0
                                              ? std::expected<std::vector<FinalityProof>, ConsensusError>(
                                          std::unexpected(ConsensusError::NotReady))
                                              : instance->engine_->finality_proofs_after(finalized_height - 1, 1);
            if (pending.value().first_seen_ms == 0
                || pending.value().first_seen_ms
                       > std::numeric_limits<std::uint64_t>::max() - MinimumRecoveryDelayMillis
                || !proofs.has_value() || proofs.value().size() != 1
                || !verify_recovery_document(pending.value().document,
                                             instance->recovery_policy_.value(),
                                             instance->engine_->validators().document(),
                                             pending.value().next_validators,
                                             instance->minimum_recovery_sequence_,
                                             finalized_height,
                                             hash_header(proofs.value().front().finalized_proposal.header),
                                             proofs.value().front().finalized_proposal.header.state_commitment)) {
                return std::unexpected(ConsensusError::InvalidGovernance);
            }
            instance->pending_recovery_ = pending.value();
        }
        return instance;
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

    std::expected<void, ConsensusError> ShadowConsensus::write_recovery_policy(
        const std::filesystem::path& directory,
        const MultisigPolicy&        policy) {
        if (!verify_multisig_policy(policy) || policy.public_keys.size() != GovernanceSignerCount
            || policy.threshold != RecoveryThreshold) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        return write_document(directory / RecoveryPolicyFile, policy);
    }

    std::expected<void, ConsensusError> ShadowConsensus::write_trust_anchor(const std::filesystem::path& directory,
                                                                            const TrustAnchorV1&         anchor) {
        if (!verify_trust_anchor(anchor)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            return std::unexpected(ConsensusError::StorageUnavailable);
        }
        return write_document(directory / TrustAnchorFile, anchor);
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
        auto proposal = engine_->make_proposal(std::move(checkpoint.batch), std::move(checkpoint.state), round);
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
        next_history.push_back(EpochStartV1 {
            .kind              = EpochStartKind::Normal,
            .validators        = transition.next_validators,
            .bootstrap         = transition.bootstrap,
            .normal_transition = transition,
        });
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

    std::expected<void, ConsensusError> ShadowConsensus::schedule_recovery(const RecoveryDocumentV2& recovery,
                                                                           ValidatorSet  next_validators,
                                                                           std::uint64_t now_ms) {
        if (configuration_.mode != ShadowMode::Finality || !recovery_policy_.has_value() || now_ms == 0
            || now_ms > std::numeric_limits<std::uint64_t>::max() - MinimumRecoveryDelayMillis) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        const auto finalized_height = engine_->safety_state().finalized_height;
        if (finalized_height == 0) {
            return std::unexpected(ConsensusError::NotReady);
        }
        const auto proofs = engine_->finality_proofs_after(finalized_height - 1, 1);
        if (!proofs.has_value() || proofs.value().size() != 1
            || proofs.value().front().finalized_proposal.header.height != finalized_height) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        const auto& finalized = proofs.value().front().finalized_proposal;
        if (!verify_recovery_document(recovery,
                                      recovery_policy_.value(),
                                      engine_->validators().document(),
                                      next_validators,
                                      minimum_recovery_sequence_,
                                      finalized_height,
                                      hash_header(finalized.header),
                                      finalized.header.state_commitment)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        if (pending_recovery_.has_value()) {
            const auto& pending = pending_recovery_.value();
            if (pending.document.recovery_sequence == recovery.recovery_sequence
                && recovery_action_hash(pending.document) != recovery_action_hash(recovery)) {
                return std::unexpected(ConsensusError::RecoveryConflict);
            }
            if (pending.document.recovery_sequence >= recovery.recovery_sequence) {
                return {};
            }
        }
        PendingRecoveryV1 pending {
            .document        = recovery,
            .next_validators = std::move(next_validators),
            .first_seen_ms   = now_ms,
        };
        const auto written = write_document(directory_ / PendingRecoveryFile, pending);
        if (!written.has_value()) {
            return std::unexpected(written.error());
        }
        pending_recovery_ = std::move(pending);
        return {};
    }

    std::expected<bool, ConsensusError> ShadowConsensus::activate_scheduled_recovery(std::uint64_t now_ms) {
        if (!pending_recovery_.has_value()) {
            return false;
        }
        if (!recovery_policy_.has_value()) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        const auto& pending = pending_recovery_.value();
        const auto  minimum_time =
            std::max(pending.document.activate_after_ms, pending.first_seen_ms + MinimumRecoveryDelayMillis);
        if (now_ms < minimum_time) {
            return false;
        }
        const auto finalized_height = engine_->safety_state().finalized_height;
        const auto proofs = finalized_height == 0 ? std::expected<std::vector<FinalityProof>, ConsensusError>(
                                                        std::unexpected(ConsensusError::NotReady))
                                                  : engine_->finality_proofs_after(finalized_height - 1, 1);
        if (!proofs.has_value() || proofs.value().size() != 1) {
            return std::unexpected(ConsensusError::DataUnavailable);
        }
        const auto& proof     = proofs.value().front();
        const auto& finalized = proof.finalized_proposal;
        if (!verify_recovery_document(pending.document,
                                      recovery_policy_.value(),
                                      engine_->validators().document(),
                                      pending.next_validators,
                                      minimum_recovery_sequence_,
                                      finalized_height,
                                      hash_header(finalized.header),
                                      finalized.header.state_commitment)
            || finalized.batch.last_section == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }

        EpochBootstrapV1 bootstrap {
            .network_id                         = pending.document.network_id,
            .previous_epoch                     = pending.document.current_epoch,
            .epoch                              = pending.document.activation_epoch,
            .activation_height                  = pending.document.finalized_height + 1,
            .previous_finalized_height          = pending.document.finalized_height,
            .first_dag_section                  = finalized.batch.last_section + 1,
            .previous_section_root              = finalized.header.section_root,
            .previous_state_commitment          = finalized.header.state_commitment,
            .previous_decision_certificate_hash = hash_certificate(proof.decision_certificate),
            .epoch_change_hash                  = recovery_action_hash(pending.document),
            .validator_set_hash                 = hash_validator_set(pending.next_validators),
        };
        if (!verify_epoch_bootstrap(bootstrap, pending.next_validators)) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        auto validators =
            ValidatorSetView::create_recovery_transition(pending.next_validators, pending.document.operators);
        if (!validators.has_value()) {
            return std::unexpected(validators.error());
        }
        auto identity_path = epoch_identity_path(directory_, pending.next_validators.epoch);
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
                                                  epoch_safety_path(directory_, pending.next_validators.epoch)),
                                              proposal_validator_,
                                              bootstrap);
        const auto initialized = next_engine->initialize();
        if (!initialized.has_value()) {
            return std::unexpected(initialized.error());
        }

        auto next_history = epoch_history_;
        next_history.push_back(EpochStartV1 {
            .kind       = EpochStartKind::Recovery,
            .validators = pending.next_validators,
            .bootstrap  = bootstrap,
            .recovery   = pending.document,
        });
        const auto history_written = write_document(directory_ / EpochHistoryFile, next_history);
        if (!history_written.has_value()) {
            return std::unexpected(history_written.error());
        }
        std::error_code remove_error;
        std::filesystem::remove(directory_ / PendingRecoveryFile, remove_error);
        if (remove_error) {
            return std::unexpected(ConsensusError::StorageFailure);
        }
        engine_                    = std::move(next_engine);
        active_bootstrap_          = bootstrap;
        epoch_history_             = std::move(next_history);
        minimum_recovery_sequence_ = pending.document.recovery_sequence + 1;
        pending_epoch_.reset();
        pending_recovery_.reset();
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

    const std::optional<PendingRecoveryV1>& ShadowConsensus::pending_recovery() const noexcept {
        return pending_recovery_;
    }

    std::vector<EpochStartV1> ShadowConsensus::epoch_starts() const {
        return epoch_history_;
    }

    const std::optional<TrustAnchorV1>& ShadowConsensus::trust_anchor() const noexcept {
        return trust_anchor_;
    }

    bool ShadowConsensus::peer_matches_validator(std::string_view validator_id,
                                                 std::string_view peer_identifier) const {
        const auto* validator = engine_->validators().find(validator_id);
        return validator != nullptr && validator->node_identifier == peer_identifier;
    }

} // namespace ExtraChain::Consensus
