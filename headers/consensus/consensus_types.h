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

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <msgpack.hpp>

#include "chain/actor.h"
#include "chain/actor_id.h"
#include "extrachain_global.h"

namespace ExtraChain::Consensus {

    inline constexpr std::uint16_t ProtocolVersion         = 3;
    inline constexpr std::uint64_t ShadowSectionInterval   = 20;
    inline constexpr std::uint64_t MaximumShadowBatchBytes = 64ULL * 1024ULL * 1024ULL;
    inline constexpr std::uint64_t MaximumShadowSyncBytes  = 32ULL * 1024ULL * 1024ULL;
    inline constexpr std::size_t   MaximumShadowSyncProofs = 8;

    enum class ShadowMode : std::uint8_t {
        Observe,
        Finality
    };

    enum class ValidatorStatus : std::uint8_t {
        Active,
        Observer,
        Revoked
    };

    enum class Phase : std::uint8_t {
        Genesis,
        Prepare,
        Timeout
    };

    enum class ConsensusError : std::uint8_t {
        InvalidProtocol,
        InvalidNetwork,
        InvalidEpoch,
        InvalidHeight,
        InvalidRound,
        InvalidValidatorSet,
        InvalidValidator,
        InvalidLeader,
        InvalidSignature,
        InvalidCertificate,
        InvalidParent,
        InvalidRoot,
        UnsafeProposal,
        DuplicateVote,
        ConflictingVote,
        StorageUnavailable,
        StorageFailure,
        DataUnavailable,
        DataTooLarge,
        NotValidator,
        NotLeader,
        NotReady,
        Replay,
        InvalidIntent,
        InvalidNonce,
        IntentExpired,
        DuplicateIntent,
        PoolFull,
        InvalidProof,
        InvalidGovernance
    };

    struct SectionBatchManifest {
        std::uint64_t            first_section = 0;
        std::uint64_t            last_section  = 0;
        std::vector<std::string> transaction_hashes;
        std::string              transaction_root;
        std::string              data_root;
        std::string              previous_section_root;
        std::uint64_t            payload_bytes = 0;

        MSGPACK_DEFINE(first_section,
                       last_section,
                       transaction_hashes,
                       transaction_root,
                       data_root,
                       previous_section_root,
                       payload_bytes)
    };

    struct SectionBatchData {
        std::string                                        header_hash;
        SectionBatchManifest                               manifest;
        std::vector<std::pair<std::uint64_t, std::string>> sections;

        MSGPACK_DEFINE(header_hash, manifest, sections)
    };

    struct ValidatorRecord {
        std::uint16_t   protocol_version = ProtocolVersion;
        ActorId         network_id;
        std::uint64_t   epoch = 0;
        std::string     validator_id;
        ActorId         actor_id;
        std::string     actor_public_key;
        std::string     consensus_public_key;
        std::string     node_identifier;
        std::uint64_t   valid_from  = 0;
        std::uint64_t   valid_until = 0;
        ValidatorStatus status      = ValidatorStatus::Active;
        std::string     actor_signature;
        std::string     consensus_signature;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       validator_id,
                       actor_id,
                       actor_public_key,
                       consensus_public_key,
                       node_identifier,
                       valid_from,
                       valid_until,
                       status,
                       actor_signature,
                       consensus_signature)
    };

    struct ValidatorSet {
        std::uint16_t                protocol_version = ProtocolVersion;
        ActorId                      network_id;
        std::uint64_t                epoch = 0;
        std::vector<ValidatorRecord> validators;
        std::string                  governance_public_key;
        std::string                  governance_signature;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       validators,
                       governance_public_key,
                       governance_signature)
    };

    struct ConsensusHeader {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch       = 0;
        std::uint64_t height      = 0;
        std::uint64_t round       = 0;
        std::uint64_t dag_section = 0;
        std::string   parent_certificate_hash;
        std::string   section_root;
        std::string   transaction_root;
        std::string   batch_root;
        std::string   validator_set_hash;
        std::string   state_commitment;
        std::uint64_t logical_time = 0;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       height,
                       round,
                       dag_section,
                       parent_certificate_hash,
                       section_root,
                       transaction_root,
                       batch_root,
                       validator_set_hash,
                       state_commitment,
                       logical_time)
    };

    struct QuorumCertificate {
        std::uint16_t             protocol_version = ProtocolVersion;
        ActorId                   network_id;
        std::uint64_t             epoch  = 0;
        std::uint64_t             height = 0;
        std::uint64_t             round  = 0;
        Phase                     phase  = Phase::Prepare;
        std::string               header_hash;
        std::vector<std::uint8_t> signer_bitmap;
        std::vector<std::string>  signatures;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       height,
                       round,
                       phase,
                       header_hash,
                       signer_bitmap,
                       signatures)
    };

    struct Vote {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch  = 0;
        std::uint64_t height = 0;
        std::uint64_t round  = 0;
        Phase         phase  = Phase::Prepare;
        std::string   header_hash;
        std::string   validator_id;
        std::string   signature;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       height,
                       round,
                       phase,
                       header_hash,
                       validator_id,
                       signature)
    };

    struct EquivocationProof {
        Vote first;
        Vote second;

        MSGPACK_DEFINE(first, second)
    };

    struct TimeoutVote {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch  = 0;
        std::uint64_t height = 0;
        std::uint64_t round  = 0;
        std::string   highest_certificate_hash;
        std::string   validator_id;
        std::string   signature;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       height,
                       round,
                       highest_certificate_hash,
                       validator_id,
                       signature)
    };

    struct TimeoutCertificate {
        std::uint16_t             protocol_version = ProtocolVersion;
        ActorId                   network_id;
        std::uint64_t             epoch  = 0;
        std::uint64_t             height = 0;
        std::uint64_t             round  = 0;
        QuorumCertificate         highest_certificate;
        std::vector<std::uint8_t> signer_bitmap;
        std::vector<std::string>  signatures;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       height,
                       round,
                       highest_certificate,
                       signer_bitmap,
                       signatures)
    };

    struct Proposal {
        ConsensusHeader                   header;
        SectionBatchManifest              batch;
        QuorumCertificate                 parent_certificate;
        std::optional<TimeoutCertificate> timeout_certificate;
        std::string                       proposer_id;
        std::string                       signature;

        MSGPACK_DEFINE(header, batch, parent_certificate, timeout_certificate, proposer_id, signature)
    };

    struct TimeoutAcceptance {
        std::optional<TimeoutCertificate>                  certificate;
        std::optional<std::pair<TimeoutVote, TimeoutVote>> equivocation;
    };

    struct SafetyState {
        std::uint16_t                     protocol_version = ProtocolVersion;
        ActorId                           network_id;
        std::uint64_t                     epoch             = 0;
        std::uint64_t                     last_voted_height = 0;
        std::uint64_t                     last_voted_round  = 0;
        Phase                             last_voted_phase  = Phase::Genesis;
        std::string                       last_voted_hash;
        std::optional<QuorumCertificate>  highest_certificate;
        std::optional<QuorumCertificate>  locked_certificate;
        std::uint64_t                     finalized_height    = 0;
        std::uint64_t                     current_round       = 0;
        std::uint64_t                     last_timeout_height = 0;
        std::uint64_t                     last_timeout_round  = 0;
        std::string                       last_timeout_certificate_hash;
        std::optional<TimeoutCertificate> highest_timeout_certificate;
        std::string                       validator_set_hash;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       last_voted_height,
                       last_voted_round,
                       last_voted_phase,
                       last_voted_hash,
                       highest_certificate,
                       locked_certificate,
                       finalized_height,
                       current_round,
                       last_timeout_height,
                       last_timeout_round,
                       last_timeout_certificate_hash,
                       highest_timeout_certificate,
                       validator_set_hash)
    };

    struct FinalizedCheckpoint {
        std::uint64_t height            = 0;
        std::uint64_t dag_section       = 0;
        std::uint64_t first_dag_section = 0;
        std::string   header_hash;
        std::string   section_root;
        std::string   transaction_root;
        std::string   batch_root;
        std::string   certificate_hash;

        MSGPACK_DEFINE(height,
                       dag_section,
                       first_dag_section,
                       header_hash,
                       section_root,
                       transaction_root,
                       batch_root,
                       certificate_hash)
    };

    struct FinalityProof {
        Proposal          finalized_proposal;
        Proposal          child_proposal;
        Proposal          grandchild_proposal;
        QuorumCertificate decision_certificate;

        MSGPACK_DEFINE(finalized_proposal, child_proposal, grandchild_proposal, decision_certificate)
    };

    struct ConsensusMetricsSnapshot {
        std::uint64_t proposals_created = 0;
        std::uint64_t batches_staged    = 0;
        std::uint64_t votes_created     = 0;
        std::uint64_t timeout_votes     = 0;
        std::uint64_t certificates      = 0;
        std::uint64_t finalized         = 0;
        std::uint64_t stage_nanoseconds = 0;
    };

    struct ShadowConfiguration {
        std::uint16_t protocol_version       = ProtocolVersion;
        ShadowMode    mode                   = ShadowMode::Observe;
        std::uint64_t activation_height      = 0;
        std::uint64_t activation_dag_section = 0;
        std::uint64_t proposal_timeout_ms    = 4'000;
        std::uint64_t maximum_timeout_ms     = 30'000;
        std::uint64_t maximum_batch_bytes    = 16ULL * 1024ULL * 1024ULL;

        MSGPACK_DEFINE(protocol_version,
                       mode,
                       activation_height,
                       activation_dag_section,
                       proposal_timeout_ms,
                       maximum_timeout_ms,
                       maximum_batch_bytes)
    };

    struct SectionBatchRequest {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch = 0;
        std::string   header_hash;

        MSGPACK_DEFINE(protocol_version, network_id, epoch, header_hash)
    };

    struct ShadowSyncRequest {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch            = 0;
        std::uint64_t finalized_height = 0;

        MSGPACK_DEFINE(protocol_version, network_id, epoch, finalized_height)
    };

    struct ShadowSyncResponse {
        std::uint16_t                 protocol_version = ProtocolVersion;
        ActorId                       network_id;
        std::uint64_t                 epoch = 0;
        std::vector<FinalityProof>    proofs;
        std::vector<SectionBatchData> batches;

        MSGPACK_DEFINE(protocol_version, network_id, epoch, proofs, batches)
    };

    struct AuthenticationChallenge {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch = 0;
        std::string   challenge_id;
        std::string   challenger_node_identifier;
        std::string   target_node_identifier;
        std::uint64_t expires_at_millis = 0;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       challenge_id,
                       challenger_node_identifier,
                       target_node_identifier,
                       expires_at_millis)
    };

    struct AuthenticationResponse {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch = 0;
        std::string   challenge_hash;
        std::string   validator_id;
        std::string   node_identifier;
        std::string   signature;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       challenge_hash,
                       validator_id,
                       node_identifier,
                       signature)
    };

    EXTRACHAIN_EXPORT std::string hash_validator_record(const ValidatorRecord& record);
    EXTRACHAIN_EXPORT std::string hash_validator_set(const ValidatorSet& validators);
    EXTRACHAIN_EXPORT std::string hash_header(const ConsensusHeader& header);
    EXTRACHAIN_EXPORT std::string hash_batch_manifest(const SectionBatchManifest& manifest);
    EXTRACHAIN_EXPORT std::string calculate_transaction_root(const std::vector<std::string>& hashes);
    EXTRACHAIN_EXPORT std::string calculate_data_root(
        const std::vector<std::pair<std::uint64_t, std::string>>& sections);
    EXTRACHAIN_EXPORT std::string hash_certificate(const QuorumCertificate& certificate);
    EXTRACHAIN_EXPORT std::string calculate_consensus_state_commitment(const QuorumCertificate& parent,
                                                                       std::string_view         section_root,
                                                                       std::string_view         batch_root,
                                                                       std::string_view validator_set_hash);
    EXTRACHAIN_EXPORT std::string hash_timeout_certificate(const TimeoutCertificate& certificate);
    EXTRACHAIN_EXPORT std::string proposal_signing_payload(const Proposal& proposal);
    EXTRACHAIN_EXPORT std::string vote_signing_payload(const Vote& vote);
    EXTRACHAIN_EXPORT std::string timeout_vote_signing_payload(const TimeoutVote& vote);
    EXTRACHAIN_EXPORT std::string hash_authentication_challenge(const AuthenticationChallenge& challenge);
    EXTRACHAIN_EXPORT std::string authentication_response_signing_payload(const AuthenticationResponse& response);
    EXTRACHAIN_EXPORT std::string validator_binding_payload(const ValidatorRecord& record);
    EXTRACHAIN_EXPORT std::string validator_set_signing_payload(const ValidatorSet& validators);
    EXTRACHAIN_EXPORT std::string validator_id_for(const PublicKey& public_key);
    EXTRACHAIN_EXPORT std::expected<std::string, ConsensusError> sign_payload(const KeyPrivate&  key,
                                                                              const std::string& payload);
    EXTRACHAIN_EXPORT bool                                       verify_payload(const std::string& public_key,
                                                                                const std::string& payload,
                                                                                const std::string& signature);

} // namespace ExtraChain::Consensus

MSGPACK_ADD_ENUM(ExtraChain::Consensus::ValidatorStatus)
MSGPACK_ADD_ENUM(ExtraChain::Consensus::Phase)
MSGPACK_ADD_ENUM(ExtraChain::Consensus::ShadowMode)
MSGPACK_ADD_ENUM(ExtraChain::Consensus::ConsensusError)
