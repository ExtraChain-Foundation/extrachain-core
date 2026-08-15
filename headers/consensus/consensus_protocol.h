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
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "consensus/consensus_types.h"

class Transaction;

namespace ExtraChain::Consensus {

    inline constexpr std::size_t   ShadowCommitteeSize   = 7;
    inline constexpr std::size_t   GovernanceSignerCount = 5;
    inline constexpr std::uint16_t GovernanceThreshold   = 3;
    inline constexpr std::uint16_t RecoveryThreshold     = 4;

    enum class IntentOperation : std::uint8_t {
        Transfer,
        ContractDeploy,
        ContractCall,
        ContractUpgrade,
        TokenMigration,
        EpochChange
    };

    enum class IntentStatus : std::uint8_t {
        Accepted,
        Certified,
        Finalized,
        Rejected,
        Expired
    };

    struct TransactionIntentV2 {
        std::uint16_t   protocol_version = ProtocolVersion;
        ActorId         network_id;
        ActorId         sender;
        ActorId         receiver;
        ActorId         token;
        std::string     amount;
        IntentOperation operation = IntentOperation::Transfer;
        std::string     metadata_hash;
        std::uint64_t   account_nonce        = 0;
        std::uint64_t   valid_after_height   = 0;
        std::uint64_t   expires_after_height = 0;
        std::string     signature;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       sender,
                       receiver,
                       token,
                       amount,
                       operation,
                       metadata_hash,
                       account_nonce,
                       valid_after_height,
                       expires_after_height,
                       signature)
    };

    struct IntentEnvelope {
        TransactionIntentV2 intent;
        std::string         metadata;

        MSGPACK_DEFINE(intent, metadata)
    };

    struct IntentReceipt {
        std::string    intent_hash;
        IntentStatus   status           = IntentStatus::Accepted;
        std::uint64_t  consensus_height = 0;
        std::uint64_t  dag_section      = 0;
        std::uint32_t  position         = 0;
        ConsensusError error            = ConsensusError::NotReady;

        MSGPACK_DEFINE(intent_hash, status, consensus_height, dag_section, position, error)
    };

    struct MerkleProof {
        std::uint64_t            leaf_index = 0;
        std::uint64_t            leaf_count = 0;
        std::string              leaf_hash;
        std::vector<std::string> siblings;

        MSGPACK_DEFINE(leaf_index, leaf_count, leaf_hash, siblings)
    };

    struct IndexedSignature {
        std::uint16_t signer_index = 0;
        std::string   signature;

        MSGPACK_DEFINE(signer_index, signature)
    };

    struct MultisigPolicy {
        std::uint16_t            protocol_version = ProtocolVersion;
        ActorId                  network_id;
        std::uint16_t            threshold = 0;
        std::vector<std::string> public_keys;
        std::string              policy_hash;

        MSGPACK_DEFINE(protocol_version, network_id, threshold, public_keys, policy_hash)
    };

    struct GovernanceAuthorization {
        std::uint16_t                 protocol_version = ProtocolVersion;
        ActorId                       network_id;
        std::uint64_t                 sequence = 0;
        std::string                   action_hash;
        std::vector<IndexedSignature> signatures;

        MSGPACK_DEFINE(protocol_version, network_id, sequence, action_hash, signatures)
    };

    struct OperatorAttestation {
        std::string   operator_id_hash;
        ActorId       actor_id;
        std::string   node_identifier;
        std::string   consensus_public_key;
        std::string   document_hash;
        std::uint32_t policy_version = 1;

        MSGPACK_DEFINE(operator_id_hash,
                       actor_id,
                       node_identifier,
                       consensus_public_key,
                       document_hash,
                       policy_version)
    };

    struct EpochChangeV1 {
        std::uint16_t                    protocol_version = ProtocolVersion;
        ActorId                          network_id;
        std::uint64_t                    current_epoch     = 0;
        std::uint64_t                    activation_epoch  = 0;
        std::uint64_t                    activation_height = 0;
        std::string                      current_validator_set_hash;
        std::string                      next_validator_set_hash;
        std::string                      registry_document_hash;
        std::vector<OperatorAttestation> operators;
        GovernanceAuthorization          authorization;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       current_epoch,
                       activation_epoch,
                       activation_height,
                       current_validator_set_hash,
                       next_validator_set_hash,
                       registry_document_hash,
                       operators,
                       authorization)
    };

    struct EpochBootstrapV1 {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t previous_epoch            = 0;
        std::uint64_t epoch                     = 0;
        std::uint64_t activation_height         = 0;
        std::uint64_t previous_finalized_height = 0;
        std::uint64_t first_dag_section         = 0;
        std::string   previous_section_root;
        std::string   previous_state_commitment;
        std::string   previous_decision_certificate_hash;
        std::string   epoch_change_hash;
        std::string   validator_set_hash;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       previous_epoch,
                       epoch,
                       activation_height,
                       previous_finalized_height,
                       first_dag_section,
                       previous_section_root,
                       previous_state_commitment,
                       previous_decision_certificate_hash,
                       epoch_change_hash,
                       validator_set_hash)
    };

    struct EpochChangeRequestV1 {
        std::uint16_t protocol_version = ProtocolVersion;
        EpochChangeV1 change;
        ValidatorSet  next_validators;

        MSGPACK_DEFINE(protocol_version, change, next_validators)
    };

    struct RecoveryDocumentV1 {
        std::uint16_t           protocol_version = ProtocolVersion;
        ActorId                 network_id;
        std::uint64_t           recovery_sequence = 0;
        std::uint64_t           finalized_height  = 0;
        std::string             finalized_checkpoint_hash;
        std::string             next_validator_set_hash;
        std::string             registry_document_hash;
        GovernanceAuthorization authorization;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       recovery_sequence,
                       finalized_height,
                       finalized_checkpoint_hash,
                       next_validator_set_hash,
                       registry_document_hash,
                       authorization)
    };

    struct ActivationManifestV1 {
        std::uint16_t           protocol_version = ProtocolVersion;
        ActorId                 network_id;
        std::uint64_t           activation_height      = 0;
        std::uint64_t           activation_dag_section = 0;
        std::string             validator_set_hash;
        bool                    require_intent_v2 = true;
        GovernanceAuthorization authorization;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       activation_height,
                       activation_dag_section,
                       validator_set_hash,
                       require_intent_v2,
                       authorization)
    };

    struct StateCommitmentV2 {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch  = 0;
        std::uint64_t height = 0;
        std::string   previous_state_commitment;
        std::string   section_root;
        std::string   account_state_root;
        std::string   contract_state_root;
        std::string   token_registry_root;
        std::string   validator_set_hash;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       epoch,
                       height,
                       previous_state_commitment,
                       section_root,
                       account_state_root,
                       contract_state_root,
                       token_registry_root,
                       validator_set_hash)
    };

    struct TransactionInclusionProofV1 {
        std::uint16_t protocol_version = ProtocolVersion;
        ActorId       network_id;
        std::uint64_t epoch  = 0;
        std::uint64_t height = 0;
        std::string   transaction_hash;
        MerkleProof   merkle_proof;
        FinalityProof finality_proof;

        MSGPACK_DEFINE(protocol_version, network_id, epoch, height, transaction_hash, merkle_proof, finality_proof)
    };

    struct EpochTransitionV1 {
        std::uint16_t               protocol_version = ProtocolVersion;
        EpochChangeV1               change;
        ValidatorSet                next_validators;
        TransactionInclusionProofV1 proof;
        EpochBootstrapV1            bootstrap;

        MSGPACK_DEFINE(protocol_version, change, next_validators, proof, bootstrap)
    };

    struct IntentPoolLimits {
        std::size_t   maximum_intents        = 16'384;
        std::size_t   maximum_bytes          = 64ULL * 1024ULL * 1024ULL;
        std::size_t   maximum_sender_intents = 64;
        std::uint64_t maximum_nonce_gap      = 64;
        std::size_t   maximum_metadata_bytes = 256ULL * 1024ULL;
    };

    class EXTRACHAIN_EXPORT IntentPool {
    public:
        explicit IntentPool(IntentPoolLimits limits = {});

        std::expected<std::string, ConsensusError> submit(const IntentEnvelope& envelope,
                                                          std::string_view      sender_public_key,
                                                          std::uint64_t         committed_nonce,
                                                          std::uint64_t         current_height);
        [[nodiscard]] std::vector<IntentEnvelope>  ready(const std::map<ActorId, std::uint64_t>& committed_nonces,
                                                         std::uint64_t                           current_height,
                                                         std::size_t                             maximum_count,
                                                         std::size_t maximum_bytes) const;
        std::vector<std::string>                   expire(std::uint64_t current_height);
        void                                       erase(const std::vector<std::string>& intent_hashes);

        [[nodiscard]] std::size_t size() const noexcept;
        [[nodiscard]] std::size_t bytes() const noexcept;

    private:
        struct Entry {
            IntentEnvelope envelope;
            std::size_t    bytes = 0;
        };

        IntentPoolLimits               limits_;
        std::map<std::string, Entry>   entries_;
        std::map<ActorId, std::size_t> sender_counts_;
        std::size_t                    bytes_ = 0;
    };

    EXTRACHAIN_EXPORT std::string intent_metadata_hash(std::string_view metadata);
    EXTRACHAIN_EXPORT std::string intent_signing_payload(const TransactionIntentV2& intent);
    EXTRACHAIN_EXPORT std::string hash_intent(const TransactionIntentV2& intent);
    EXTRACHAIN_EXPORT std::expected<TransactionIntentV2, ConsensusError> make_intent(
        TransactionIntentV2      intent,
        std::string_view         metadata,
        const Actor<KeyPrivate>& sender);
    EXTRACHAIN_EXPORT bool verify_intent(const IntentEnvelope& envelope, std::string_view sender_public_key);
    EXTRACHAIN_EXPORT std::expected<Transaction, ConsensusError> materialize_intent(
        const IntentEnvelope&        envelope,
        std::uint64_t                section,
        std::uint64_t                logical_time,
        const std::set<std::string>& previous_hashes);
    EXTRACHAIN_EXPORT std::expected<IntentEnvelope, ConsensusError> intent_from_transaction(
        const Transaction& transaction);
    EXTRACHAIN_EXPORT bool verify_materialized_intent(const Transaction& transaction,
                                                      std::string_view   sender_public_key);
    EXTRACHAIN_EXPORT std::string consensus_transaction_hash(const Transaction& transaction);

    EXTRACHAIN_EXPORT std::string merkle_root(const std::vector<std::string>& values);
    EXTRACHAIN_EXPORT std::expected<MerkleProof, ConsensusError> make_merkle_proof(
        const std::vector<std::string>& values,
        std::size_t                     index);
    EXTRACHAIN_EXPORT bool verify_merkle_proof(std::string_view   value,
                                               const MerkleProof& proof,
                                               std::string_view   expected_root);

    EXTRACHAIN_EXPORT std::expected<MultisigPolicy, ConsensusError> make_multisig_policy(
        const ActorId&           network_id,
        std::uint16_t            threshold,
        std::vector<std::string> public_keys);
    EXTRACHAIN_EXPORT bool verify_multisig_policy(const MultisigPolicy& policy);
    EXTRACHAIN_EXPORT std::expected<GovernanceAuthorization, ConsensusError> authorize_action(
        const MultisigPolicy&          policy,
        std::uint64_t                  sequence,
        std::string                    action_hash,
        const std::vector<KeyPrivate>& signers);
    EXTRACHAIN_EXPORT bool verify_authorization(const MultisigPolicy&          policy,
                                                const GovernanceAuthorization& authorization,
                                                std::uint64_t                  minimum_sequence);
    EXTRACHAIN_EXPORT std::string epoch_change_action_hash(const EpochChangeV1& change);
    EXTRACHAIN_EXPORT bool        verify_epoch_change(const EpochChangeV1&  change,
                                                      const MultisigPolicy& policy,
                                                      std::uint64_t         current_height,
                                                      std::uint64_t         minimum_sequence);
    EXTRACHAIN_EXPORT std::expected<EpochBootstrapV1, ConsensusError> make_epoch_bootstrap(
        const EpochChangeV1&               change,
        const ValidatorSet&                next_validators,
        const TransactionInclusionProofV1& proof);
    EXTRACHAIN_EXPORT std::string hash_epoch_bootstrap(const EpochBootstrapV1& bootstrap);
    EXTRACHAIN_EXPORT bool        verify_epoch_bootstrap(const EpochBootstrapV1& bootstrap,
                                                         const ValidatorSet&     next_validators);
    EXTRACHAIN_EXPORT std::string recovery_action_hash(const RecoveryDocumentV1& recovery);
    EXTRACHAIN_EXPORT bool        verify_recovery_document(const RecoveryDocumentV1& recovery,
                                                           const MultisigPolicy&     recovery_policy,
                                                           std::uint64_t             expected_sequence,
                                                           std::uint64_t             expected_finalized_height,
                                                           std::string_view          expected_checkpoint_hash);
    EXTRACHAIN_EXPORT std::string activation_action_hash(const ActivationManifestV1& activation);
    EXTRACHAIN_EXPORT bool        verify_activation_manifest(const ActivationManifestV1& activation,
                                                             const MultisigPolicy&       policy,
                                                             std::uint64_t               current_height,
                                                             std::uint64_t               minimum_sequence);
    EXTRACHAIN_EXPORT std::string hash_state_commitment(const StateCommitmentV2& commitment);

} // namespace ExtraChain::Consensus

MSGPACK_ADD_ENUM(ExtraChain::Consensus::IntentOperation)
MSGPACK_ADD_ENUM(ExtraChain::Consensus::IntentStatus)
