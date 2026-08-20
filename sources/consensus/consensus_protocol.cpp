/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/consensus_protocol.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <queue>
#include <set>
#include <tuple>

#include "chain/transaction.h"
#include "consensus/validator_set.h"
#include "core/byte_array.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view IntentMetadataDomain = "EXC_INTENT_METADATA_V2";
        constexpr std::string_view IntentDomain         = "EXC_TRANSACTION_INTENT_V2";
        constexpr std::string_view MerkleLeafDomain     = "EXC_SHADOW_TRANSACTION_LEAF_V2";
        constexpr std::string_view MerkleNodeDomain     = "EXC_SHADOW_TRANSACTION_NODE_V2";
        constexpr std::string_view PolicyDomain         = "EXC_GOVERNANCE_POLICY_V1";
        constexpr std::string_view AuthorizationDomain  = "EXC_GOVERNANCE_AUTHORIZATION_V1";
        constexpr std::string_view EpochChangeDomain    = "EXC_EPOCH_CHANGE_V1";
        constexpr std::string_view EpochBootstrapDomain = "EXC_EPOCH_BOOTSTRAP_V1";
        constexpr std::string_view RecoveryV2Domain     = "EXC_EMERGENCY_RECOVERY_V2";
        constexpr std::string_view TrustAnchorDomain    = "EXC_SHADOW_TRUST_ANCHOR_V1";
        constexpr std::string_view ActivationDomain     = "EXC_SHADOW_ACTIVATION_V1";
        constexpr std::string_view StateDomain          = "EXC_STATE_COMMITMENT_V2";
        constexpr std::string_view StateKeyDomain       = "EXC_STATE_KEY_V1";
        constexpr std::string_view StateLeafDomain      = "EXC_STATE_LEAF_V1";
        constexpr std::string_view StateBucketDomain    = "EXC_STATE_BUCKET_V1";
        constexpr std::string_view StateRootDomain      = "EXC_SEGMENTED_STATE_ROOT_V1";
        constexpr std::uint64_t    MaximumStoredHeight =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

        template <typename T>
        std::string domain_hash(std::string_view domain, const T& value) {
            return Utils::calculate_hash(std::string(domain) + MessagePack::serialize(value),
                                         Utils::HashAlgorithm::Blake3);
        }

        std::optional<ActorId> actor_id_for(std::string_view encoded_public_key) {
            const auto decoded = ByteArray::fromBase64(encoded_public_key);
            if (!decoded.has_value() || decoded.value().size() != crypto_sign_PUBLICKEYBYTES) {
                return std::nullopt;
            }
            const auto hash = Utils::calculate_hash(decoded.value().toString(), Utils::HashAlgorithm::Blake3);
            const auto id   = ActorId::create(hash.substr(0, ActorId::SIZE));
            return id.has_value() ? std::optional<ActorId>(id.value()) : std::nullopt;
        }

        std::string merkle_leaf(std::string_view value) {
            return Utils::calculate_hash(std::string(MerkleLeafDomain) + std::string(value),
                                         Utils::HashAlgorithm::Blake3);
        }

        std::string merkle_parent(std::string_view left, std::string_view right) {
            return Utils::calculate_hash(std::string(MerkleNodeDomain) + std::string(left) + std::string(right),
                                         Utils::HashAlgorithm::Blake3);
        }

        std::vector<std::string> next_merkle_level(const std::vector<std::string>& level) {
            std::vector<std::string> next;
            next.reserve((level.size() + 1) / 2);
            for (std::size_t index = 0; index < level.size(); index += 2) {
                const auto& right = index + 1 < level.size() ? level[index + 1] : level[index];
                next.push_back(merkle_parent(level[index], right));
            }
            return next;
        }

        bool valid_amount(std::string_view amount) {
            if (amount.empty() || amount.size() > 128 || amount.front() == '-' || amount.front() == '+'
                || amount.front() == '.' || amount.back() == '.') {
                return false;
            }
            const auto decimal = amount.find('.');
            if (decimal != std::string_view::npos && amount.find('.', decimal + 1) != std::string_view::npos) {
                return false;
            }
            const auto integer_digits = decimal == std::string_view::npos ? amount.size() : decimal;
            if (integer_digits > 1 && amount.front() == '0') {
                return false;
            }
            return std::ranges::all_of(amount, [](char value) {
                return value == '.' || (value >= '0' && value <= '9');
            });
        }

        bool valid_operation(IntentOperation operation) {
            switch (operation) {
            case IntentOperation::Transfer:
            case IntentOperation::ContractDeploy:
            case IntentOperation::ContractCall:
            case IntentOperation::ContractUpgrade:
            case IntentOperation::TokenMigration:
            case IntentOperation::EpochChange:
                return true;
            }
            return false;
        }

        bool positive_amount(std::string_view amount) {
            return valid_amount(amount) && std::ranges::any_of(amount, [](char value) {
                       return value >= '1' && value <= '9';
                   });
        }

        std::optional<TransactionType> transaction_type(IntentOperation operation) {
            switch (operation) {
            case IntentOperation::Transfer:
                return TransactionType::Regular;
            case IntentOperation::ContractDeploy:
                return TransactionType::ContractDeploy;
            case IntentOperation::ContractCall:
                return TransactionType::ContractCall;
            case IntentOperation::ContractUpgrade:
                return TransactionType::ContractUpgrade;
            case IntentOperation::TokenMigration:
                return TransactionType::TokenMigration;
            case IntentOperation::EpochChange:
                return TransactionType::EpochChange;
            }
            return std::nullopt;
        }

        std::string unsigned_epoch_change_hash(const EpochChangeV1& change) {
            return domain_hash(EpochChangeDomain,
                               std::tuple { change.protocol_version,
                                            change.network_id,
                                            change.current_epoch,
                                            change.activation_epoch,
                                            change.activation_height,
                                            change.current_validator_set_hash,
                                            change.next_validator_set_hash,
                                            change.registry_document_hash,
                                            change.operators });
        }

        std::string unsigned_trust_anchor_hash(const TrustAnchorV1& anchor) {
            return domain_hash(TrustAnchorDomain,
                               std::tuple { anchor.protocol_version,
                                            anchor.network_id,
                                            hash_validator_set(anchor.initial_validators),
                                            anchor.governance_policy.policy_hash,
                                            anchor.recovery_policy.policy_hash,
                                            anchor.minimum_recovery_delay_ms });
        }

        std::string unsigned_recovery_hash(const RecoveryDocumentV2& recovery) {
            auto operators = recovery.operators;
            std::ranges::sort(operators, {}, [](const OperatorAttestation& item) {
                return std::tuple { item.operator_id_hash,
                                    item.actor_id.to_string(),
                                    item.node_identifier,
                                    item.consensus_public_key };
            });
            return domain_hash(RecoveryV2Domain,
                               std::tuple { recovery.protocol_version,
                                            recovery.network_id,
                                            recovery.recovery_sequence,
                                            recovery.current_epoch,
                                            recovery.activation_epoch,
                                            recovery.finalized_height,
                                            recovery.finalized_header_hash,
                                            recovery.finalized_state_commitment,
                                            recovery.current_validator_set_hash,
                                            recovery.next_validator_set_hash,
                                            recovery.registry_document_hash,
                                            operators,
                                            recovery.signed_at_ms,
                                            recovery.activate_after_ms });
        }

        std::string unsigned_activation_hash(const ActivationManifestV1& activation) {
            return domain_hash(ActivationDomain,
                               std::tuple { activation.protocol_version,
                                            activation.network_id,
                                            activation.activation_height,
                                            activation.activation_dag_section,
                                            activation.validator_set_hash,
                                            activation.require_intent_v2 });
        }
    } // namespace

    std::string intent_metadata_hash(std::string_view metadata) {
        return Utils::calculate_hash(std::string(IntentMetadataDomain) + std::string(metadata),
                                     Utils::HashAlgorithm::Blake3);
    }

    std::string intent_signing_payload(const TransactionIntentV2& intent) {
        return std::string(IntentDomain)
               + MessagePack::serialize(std::tuple { intent.protocol_version,
                                                     intent.network_id,
                                                     intent.sender,
                                                     intent.receiver,
                                                     intent.token,
                                                     intent.amount,
                                                     intent.operation,
                                                     intent.metadata_hash,
                                                     intent.account_nonce,
                                                     intent.valid_after_height,
                                                     intent.expires_after_height });
    }

    std::string hash_intent(const TransactionIntentV2& intent) {
        return Utils::calculate_hash(intent_signing_payload(intent), Utils::HashAlgorithm::Blake3);
    }

    std::expected<TransactionIntentV2, ConsensusError> make_intent(TransactionIntentV2      intent,
                                                                   std::string_view         metadata,
                                                                   const Actor<KeyPrivate>& sender) {
        if (intent.protocol_version != ProtocolVersion || intent.network_id.is_zero() || sender.empty()
            || intent.sender != sender.id() || intent.receiver.is_zero() || !valid_amount(intent.amount)
            || !valid_operation(intent.operation)
            || (intent.operation == IntentOperation::Transfer && !positive_amount(intent.amount))
            || intent.account_nonce == 0 || intent.account_nonce > MaximumStoredHeight
            || intent.valid_after_height > MaximumStoredHeight || intent.expires_after_height > MaximumStoredHeight
            || intent.expires_after_height <= intent.valid_after_height) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }
        intent.metadata_hash = intent_metadata_hash(metadata);
        const auto signature = sign_payload(sender.key(), intent_signing_payload(intent));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        intent.signature = signature.value();
        return intent;
    }

    bool verify_intent(const IntentEnvelope& envelope, std::string_view sender_public_key) {
        const auto& intent   = envelope.intent;
        const auto  actor_id = actor_id_for(sender_public_key);
        return intent.protocol_version == ProtocolVersion && !intent.network_id.is_zero() && actor_id.has_value()
               && actor_id.value() == intent.sender && !intent.receiver.is_zero() && valid_amount(intent.amount)
               && valid_operation(intent.operation)
               && (intent.operation != IntentOperation::Transfer || positive_amount(intent.amount))
               && intent.account_nonce != 0 && intent.expires_after_height > intent.valid_after_height
               && intent.account_nonce <= MaximumStoredHeight && intent.valid_after_height <= MaximumStoredHeight
               && intent.expires_after_height <= MaximumStoredHeight
               && intent.metadata_hash == intent_metadata_hash(envelope.metadata)
               && verify_payload(std::string(sender_public_key), intent_signing_payload(intent), intent.signature);
    }

    std::expected<Transaction, ConsensusError> materialize_intent(const IntentEnvelope&        envelope,
                                                                  std::uint64_t                section,
                                                                  std::uint64_t                logical_time,
                                                                  const std::set<std::string>& previous_hashes) {
        const auto type      = transaction_type(envelope.intent.operation);
        const auto amount    = BigNumberFloat::create(envelope.intent.amount);
        const auto signature = ByteArray::fromBase64(envelope.intent.signature);
        if (!type.has_value() || !amount.has_value() || !signature.has_value()
            || signature.value().size() != crypto_sign_BYTES) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }

        Transaction transaction;
        transaction.set_type(type.value());
        transaction.set_sender(envelope.intent.sender);
        transaction.set_receiver(envelope.intent.receiver);
        transaction.set_token(envelope.intent.token);
        transaction.set_amount(amount.value());
        transaction.set_section(SectionId(section));
        transaction.set_timestamp(logical_time);
        transaction.set_meta(envelope.metadata);
        transaction.set_prev_hashs(previous_hashes);
        transaction.set_consensus_intent(Utils::to_base64(MessagePack::serialize(envelope)),
                                         signature.value().toArray<crypto_sign_BYTES>());
        transaction.update_hash();
        return transaction;
    }

    std::expected<IntentEnvelope, ConsensusError> intent_from_transaction(const Transaction& transaction) {
        if (!transaction.consensus_intent().has_value()) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }
        const auto bytes = Utils::from_base64(transaction.consensus_intent().value());
        if (!bytes.has_value()) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }
        const auto envelope = MessagePack::deserialize<IntentEnvelope>(bytes.value());
        if (!envelope.has_value()) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }
        return envelope.value();
    }

    bool verify_materialized_intent(const Transaction& transaction, std::string_view sender_public_key) {
        const auto envelope = intent_from_transaction(transaction);
        if (!envelope.has_value() || !verify_intent(envelope.value(), sender_public_key)) {
            return false;
        }
        const auto type      = transaction_type(envelope.value().intent.operation);
        const auto amount    = BigNumberFloat::create(envelope.value().intent.amount);
        const auto signature = ByteArray::fromBase64(envelope.value().intent.signature);
        return type.has_value() && amount.has_value() && signature.has_value()
               && signature.value().size() == crypto_sign_BYTES && transaction.type() == type.value()
               && transaction.sender() == envelope.value().intent.sender
               && transaction.receiver() == envelope.value().intent.receiver
               && transaction.token() == envelope.value().intent.token && transaction.amount() == amount.value()
               && transaction.meta().value_or("") == envelope.value().metadata
               && transaction.signature() == signature.value().toArray<crypto_sign_BYTES>();
    }

    std::string consensus_transaction_hash(const Transaction& transaction) {
        const auto envelope = intent_from_transaction(transaction);
        if (!envelope.has_value()) {
            return transaction.hash();
        }
        if (envelope.value().intent.operation == IntentOperation::EpochChange) {
            const auto bytes = Utils::from_base64(envelope.value().metadata);
            if (bytes.has_value()) {
                const auto request = MessagePack::deserialize<EpochChangeRequestV1>(bytes.value());
                if (request.has_value() && request.value().protocol_version == ProtocolVersion) {
                    return epoch_change_action_hash(request.value().change);
                }
            }
        }
        return hash_intent(envelope.value().intent);
    }

    IntentPool::IntentPool(IntentPoolLimits limits)
        : limits_(limits) {
    }

    std::expected<std::string, ConsensusError> IntentPool::submit(const IntentEnvelope& envelope,
                                                                  std::string_view      sender_public_key,
                                                                  std::uint64_t         committed_nonce,
                                                                  std::uint64_t         current_height) {
        if (!verify_intent(envelope, sender_public_key)) {
            return std::unexpected(ConsensusError::InvalidIntent);
        }
        const auto& intent = envelope.intent;
        if (current_height > intent.expires_after_height) {
            return std::unexpected(ConsensusError::IntentExpired);
        }
        if (intent.account_nonce <= committed_nonce
            || intent.account_nonce - committed_nonce > limits_.maximum_nonce_gap) {
            return std::unexpected(ConsensusError::InvalidNonce);
        }
        if (envelope.metadata.size() > limits_.maximum_metadata_bytes) {
            return std::unexpected(ConsensusError::DataTooLarge);
        }
        const auto hash = hash_intent(intent);
        if (entries_.contains(hash)) {
            return std::unexpected(ConsensusError::DuplicateIntent);
        }
        const auto bytes        = MessagePack::serialize(envelope).size();
        const auto sender_count = sender_counts_.contains(intent.sender) ? sender_counts_.at(intent.sender) : 0;
        if (entries_.size() >= limits_.maximum_intents || sender_count >= limits_.maximum_sender_intents
            || bytes > limits_.maximum_bytes - std::min(bytes_, limits_.maximum_bytes)) {
            return std::unexpected(ConsensusError::PoolFull);
        }
        entries_.emplace(hash, Entry { .envelope = envelope, .bytes = bytes });
        ++sender_counts_[intent.sender];
        bytes_ += bytes;
        return hash;
    }

    std::vector<IntentEnvelope> IntentPool::ready(const std::map<ActorId, std::uint64_t>& committed_nonces,
                                                  std::uint64_t                           current_height,
                                                  std::size_t                             maximum_count,
                                                  std::size_t                             maximum_bytes) const {
        using SenderQueue = std::map<std::uint64_t, const Entry*>;
        std::map<ActorId, SenderQueue> by_sender;
        for (const auto& [_, entry] : entries_) {
            const auto& intent = entry.envelope.intent;
            if (current_height < intent.valid_after_height || current_height > intent.expires_after_height) {
                continue;
            }
            by_sender[intent.sender].emplace(intent.account_nonce, &entry);
        }

        struct Candidate {
            std::string   hash;
            ActorId       sender;
            std::uint64_t nonce = 0;
            const Entry*  entry = nullptr;
        };
        const auto later = [](const Candidate& left, const Candidate& right) {
            return left.hash > right.hash;
        };
        std::priority_queue<Candidate, std::vector<Candidate>, decltype(later)> candidates(later);
        for (const auto& [sender, queue] : by_sender) {
            const auto committed = committed_nonces.contains(sender) ? committed_nonces.at(sender) : 0;
            const auto next      = committed + 1;
            const auto found     = queue.find(next);
            if (found != queue.end()) {
                candidates.push(Candidate { .hash   = hash_intent(found->second->envelope.intent),
                                            .sender = sender,
                                            .nonce  = next,
                                            .entry  = found->second });
            }
        }

        std::vector<IntentEnvelope> result;
        std::size_t                 selected_bytes           = 0;
        bool                        contract_change_selected = false;
        bool                        epoch_change_selected    = false;
        while (!candidates.empty() && result.size() < maximum_count) {
            auto candidate = candidates.top();
            candidates.pop();
            if (candidate.entry->bytes > maximum_bytes - std::min(selected_bytes, maximum_bytes)) {
                continue;
            }
            const auto& intent           = candidate.entry->envelope.intent;
            const bool  changes_contract = intent.operation == IntentOperation::ContractDeploy
                                          || intent.operation == IntentOperation::ContractCall
                                          || intent.operation == IntentOperation::ContractUpgrade
                                          || intent.operation == IntentOperation::TokenMigration;
            if ((changes_contract && contract_change_selected)
                || (intent.operation == IntentOperation::EpochChange && epoch_change_selected)) {
                continue;
            }
            result.push_back(candidate.entry->envelope);
            selected_bytes += candidate.entry->bytes;
            if (changes_contract) {
                contract_change_selected = true;
            }
            epoch_change_selected = epoch_change_selected || intent.operation == IntentOperation::EpochChange;
            if (intent.operation == IntentOperation::Transfer
                && candidate.nonce < std::numeric_limits<std::uint64_t>::max()) {
                const auto& queue = by_sender.at(candidate.sender);
                const auto  next  = queue.find(candidate.nonce + 1);
                if (next != queue.end() && next->second->envelope.intent.operation == IntentOperation::Transfer) {
                    candidates.push(Candidate { .hash   = hash_intent(next->second->envelope.intent),
                                                .sender = candidate.sender,
                                                .nonce  = candidate.nonce + 1,
                                                .entry  = next->second });
                }
            }
        }
        return result;
    }

    std::vector<std::string> IntentPool::expire(std::uint64_t current_height) {
        std::vector<std::string> expired;
        for (auto iterator = entries_.begin(); iterator != entries_.end();) {
            if (iterator->second.envelope.intent.expires_after_height >= current_height) {
                ++iterator;
                continue;
            }
            expired.push_back(iterator->first);
            bytes_ -= iterator->second.bytes;
            const auto sender = iterator->second.envelope.intent.sender;
            if (--sender_counts_[sender] == 0) {
                sender_counts_.erase(sender);
            }
            iterator = entries_.erase(iterator);
        }
        return expired;
    }

    void IntentPool::erase(const std::vector<std::string>& intent_hashes) {
        for (const auto& hash : intent_hashes) {
            const auto found = entries_.find(hash);
            if (found == entries_.end()) {
                continue;
            }
            bytes_ -= found->second.bytes;
            const auto sender = found->second.envelope.intent.sender;
            if (--sender_counts_[sender] == 0) {
                sender_counts_.erase(sender);
            }
            entries_.erase(found);
        }
    }

    std::size_t IntentPool::size() const noexcept {
        return entries_.size();
    }

    std::size_t IntentPool::bytes() const noexcept {
        return bytes_;
    }

    std::string merkle_root(const std::vector<std::string>& values) {
        return calculate_transaction_root(values);
    }

    std::expected<MerkleProof, ConsensusError> make_merkle_proof(const std::vector<std::string>& values,
                                                                 std::size_t                     index) {
        if (values.empty() || index >= values.size()) {
            return std::unexpected(ConsensusError::InvalidProof);
        }
        MerkleProof proof {
            .leaf_index = index,
            .leaf_count = values.size(),
            .leaf_hash  = merkle_leaf(values[index]),
        };
        std::vector<std::string> level;
        level.reserve(values.size());
        std::ranges::transform(values, std::back_inserter(level), merkle_leaf);
        auto position = index;
        while (level.size() > 1) {
            auto sibling = position % 2 == 0 ? position + 1 : position - 1;
            if (sibling >= level.size()) {
                sibling = position;
            }
            proof.siblings.push_back(level[sibling]);
            level = next_merkle_level(level);
            position /= 2;
        }
        return proof;
    }

    bool verify_merkle_proof(std::string_view value, const MerkleProof& proof, std::string_view expected_root) {
        if (proof.leaf_count == 0 || proof.leaf_index >= proof.leaf_count
            || proof.leaf_hash != merkle_leaf(value)) {
            return false;
        }
        auto        hash          = proof.leaf_hash;
        auto        position      = proof.leaf_index;
        auto        width         = proof.leaf_count;
        std::size_t sibling_index = 0;
        while (width > 1) {
            if (sibling_index >= proof.siblings.size()) {
                return false;
            }
            hash = position % 2 == 0 ? merkle_parent(hash, proof.siblings[sibling_index])
                                     : merkle_parent(proof.siblings[sibling_index], hash);
            position /= 2;
            width = (width + 1) / 2;
            ++sibling_index;
        }
        return sibling_index == proof.siblings.size() && hash == expected_root;
    }

    std::expected<MultisigPolicy, ConsensusError> make_multisig_policy(const ActorId&           network_id,
                                                                       std::uint16_t            threshold,
                                                                       std::vector<std::string> public_keys) {
        std::ranges::sort(public_keys);
        if (network_id.is_zero() || public_keys.empty() || threshold == 0 || threshold > public_keys.size()
            || std::ranges::adjacent_find(public_keys) != public_keys.end()
            || std::ranges::any_of(public_keys, [](const auto& key) {
                   const auto decoded = ByteArray::fromBase64(key);
                   return !decoded.has_value() || decoded.value().size() != crypto_sign_PUBLICKEYBYTES;
               })) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        MultisigPolicy policy {
            .protocol_version = ProtocolVersion,
            .network_id       = network_id,
            .threshold        = threshold,
            .public_keys      = std::move(public_keys),
        };
        policy.policy_hash = domain_hash(PolicyDomain,
                                         std::tuple { policy.protocol_version,
                                                      policy.network_id,
                                                      policy.threshold,
                                                      policy.public_keys });
        return policy;
    }

    bool verify_multisig_policy(const MultisigPolicy& policy) {
        if (policy.protocol_version != ProtocolVersion || policy.network_id.is_zero() || policy.public_keys.empty()
            || policy.threshold == 0 || policy.threshold > policy.public_keys.size()
            || !std::ranges::is_sorted(policy.public_keys)
            || std::ranges::adjacent_find(policy.public_keys) != policy.public_keys.end()
            || std::ranges::any_of(policy.public_keys, [](const auto& key) {
                   const auto decoded = ByteArray::fromBase64(key);
                   return !decoded.has_value() || decoded.value().size() != crypto_sign_PUBLICKEYBYTES;
               })) {
            return false;
        }
        return policy.policy_hash
               == domain_hash(PolicyDomain,
                              std::tuple { policy.protocol_version,
                                           policy.network_id,
                                           policy.threshold,
                                           policy.public_keys });
    }

    std::string authorization_signing_payload(const MultisigPolicy&          policy,
                                              const GovernanceAuthorization& authorization) {
        return std::string(AuthorizationDomain)
               + MessagePack::serialize(std::tuple { policy.policy_hash,
                                                     authorization.protocol_version,
                                                     authorization.network_id,
                                                     authorization.sequence,
                                                     authorization.action_hash });
    }

    std::expected<GovernanceAuthorization, ConsensusError> make_authorization(const MultisigPolicy& policy,
                                                                              std::uint64_t         sequence,
                                                                              std::string           action_hash) {
        if (sequence == 0 || action_hash.empty() || !verify_multisig_policy(policy)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        return GovernanceAuthorization {
            .protocol_version = ProtocolVersion,
            .network_id       = policy.network_id,
            .sequence         = sequence,
            .action_hash      = std::move(action_hash),
        };
    }

    std::expected<IndexedSignature, ConsensusError> sign_authorization(
        const MultisigPolicy&          policy,
        const GovernanceAuthorization& authorization,
        const KeyPrivate&              signer) {
        if (!verify_multisig_policy(policy) || signer.empty() || authorization.protocol_version != ProtocolVersion
            || authorization.network_id != policy.network_id || authorization.sequence == 0
            || authorization.action_hash.empty()) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        const auto encoded = Utils::to_base64(signer.public_key());
        const auto found   = std::ranges::lower_bound(policy.public_keys, encoded);
        if (found == policy.public_keys.end() || *found != encoded) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        const auto signature = sign_payload(signer, authorization_signing_payload(policy, authorization));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        return IndexedSignature {
            .signer_index = static_cast<std::uint16_t>(std::distance(policy.public_keys.begin(), found)),
            .signature    = signature.value(),
        };
    }

    std::expected<GovernanceAuthorization, ConsensusError> assemble_authorization(
        const MultisigPolicy&         policy,
        GovernanceAuthorization       authorization,
        std::vector<IndexedSignature> signatures) {
        authorization.signatures = std::move(signatures);
        std::ranges::sort(authorization.signatures, {}, &IndexedSignature::signer_index);
        if (!verify_authorization(policy, authorization, authorization.sequence)) {
            return std::unexpected(ConsensusError::InvalidGovernance);
        }
        return authorization;
    }

    std::expected<GovernanceAuthorization, ConsensusError> authorize_action(
        const MultisigPolicy&          policy,
        std::uint64_t                  sequence,
        std::string                    action_hash,
        const std::vector<KeyPrivate>& signers) {
        auto authorization = make_authorization(policy, sequence, std::move(action_hash));
        if (!authorization.has_value()) {
            return std::unexpected(authorization.error());
        }
        std::vector<IndexedSignature> signatures;
        std::set<std::uint16_t>       used;
        for (const auto& signer : signers) {
            const auto encoded = Utils::to_base64(signer.public_key());
            const auto found   = std::ranges::lower_bound(policy.public_keys, encoded);
            if (found == policy.public_keys.end() || *found != encoded) {
                continue;
            }
            const auto signature = sign_authorization(policy, authorization.value(), signer);
            if (!signature.has_value()) {
                return std::unexpected(signature.error());
            }
            if (!used.insert(signature.value().signer_index).second) {
                continue;
            }
            signatures.push_back(signature.value());
        }
        return assemble_authorization(policy, std::move(authorization.value()), std::move(signatures));
    }

    bool verify_authorization(const MultisigPolicy&          policy,
                              const GovernanceAuthorization& authorization,
                              std::uint64_t                  minimum_sequence) {
        if (!verify_multisig_policy(policy) || authorization.protocol_version != ProtocolVersion
            || authorization.network_id != policy.network_id || authorization.sequence < minimum_sequence
            || authorization.action_hash.empty() || authorization.signatures.size() < policy.threshold
            || authorization.signatures.size() > policy.public_keys.size()) {
            return false;
        }
        const auto              payload = authorization_signing_payload(policy, authorization);
        std::set<std::uint16_t> used;
        for (const auto& item : authorization.signatures) {
            if (item.signer_index >= policy.public_keys.size() || !used.insert(item.signer_index).second
                || !verify_payload(policy.public_keys[item.signer_index], payload, item.signature)) {
                return false;
            }
        }
        return used.size() >= policy.threshold;
    }

    std::string epoch_change_action_hash(const EpochChangeV1& change) {
        return unsigned_epoch_change_hash(change);
    }

    bool verify_epoch_change(const EpochChangeV1&  change,
                             const MultisigPolicy& policy,
                             std::uint64_t         current_height,
                             std::uint64_t         minimum_sequence) {
        if (change.protocol_version != ProtocolVersion || change.network_id != policy.network_id
            || policy.public_keys.size() != GovernanceSignerCount || policy.threshold != GovernanceThreshold
            || change.current_epoch == 0 || change.activation_epoch != change.current_epoch + 2
            || change.activation_height <= current_height || change.current_validator_set_hash.empty()
            || change.next_validator_set_hash.empty() || change.registry_document_hash.empty()
            || change.authorization.action_hash != unsigned_epoch_change_hash(change)
            || change.operators.size() != ShadowCommitteeSize) {
            return false;
        }
        std::set<std::string> operator_ids;
        std::set<ActorId>     actor_ids;
        std::set<std::string> node_ids;
        std::set<std::string> consensus_keys;
        for (const auto& item : change.operators) {
            if (item.operator_id_hash.empty() || item.actor_id.is_zero() || item.node_identifier.empty()
                || item.consensus_public_key.empty() || item.document_hash.empty() || item.policy_version == 0
                || !operator_ids.insert(item.operator_id_hash).second || !actor_ids.insert(item.actor_id).second
                || !node_ids.insert(item.node_identifier).second
                || !consensus_keys.insert(item.consensus_public_key).second) {
                return false;
            }
        }
        return verify_authorization(policy, change.authorization, minimum_sequence);
    }

    std::expected<EpochBootstrapV1, ConsensusError> make_epoch_bootstrap(
        const EpochChangeV1&               change,
        const ValidatorSet&                next_validators,
        const TransactionInclusionProofV1& proof) {
        const auto& finalized  = proof.finality_proof.finalized_proposal;
        const auto& grandchild = proof.finality_proof.grandchild_proposal;
        if (proof.transaction_hash != epoch_change_action_hash(change) || proof.height != finalized.header.height
            || proof.epoch != change.current_epoch || proof.network_id != change.network_id
            || change.activation_height != proof.height + 3
            || proof.finality_proof.decision_certificate.height + 1 != change.activation_height
            || proof.finality_proof.decision_certificate.header_hash != hash_header(grandchild.header)
            || finalized.batch.last_section == std::numeric_limits<std::uint64_t>::max()
            || next_validators.network_id != change.network_id || next_validators.epoch != change.activation_epoch
            || hash_validator_set(next_validators) != change.next_validator_set_hash) {
            return std::unexpected(ConsensusError::InvalidEpoch);
        }
        return EpochBootstrapV1 {
            .network_id                         = change.network_id,
            .previous_epoch                     = change.current_epoch,
            .epoch                              = change.activation_epoch,
            .activation_height                  = change.activation_height,
            .previous_finalized_height          = proof.height,
            .first_dag_section                  = finalized.batch.last_section + 1,
            .previous_section_root              = finalized.header.section_root,
            .previous_state_commitment          = finalized.header.state_commitment,
            .previous_decision_certificate_hash = hash_certificate(proof.finality_proof.decision_certificate),
            .epoch_change_hash                  = epoch_change_action_hash(change),
            .validator_set_hash                 = hash_validator_set(next_validators),
        };
    }

    std::string hash_epoch_bootstrap(const EpochBootstrapV1& bootstrap) {
        return domain_hash(EpochBootstrapDomain, bootstrap);
    }

    bool verify_epoch_bootstrap(const EpochBootstrapV1& bootstrap, const ValidatorSet& next_validators) {
        const bool normal_transition =
            bootstrap.previous_epoch <= std::numeric_limits<std::uint64_t>::max() - 2
            && bootstrap.previous_finalized_height <= std::numeric_limits<std::uint64_t>::max() - 3
            && bootstrap.previous_epoch + 2 == bootstrap.epoch
            && bootstrap.previous_finalized_height + 3 == bootstrap.activation_height;
        const bool recovery_transition =
            bootstrap.previous_epoch != std::numeric_limits<std::uint64_t>::max()
            && bootstrap.previous_finalized_height != std::numeric_limits<std::uint64_t>::max()
            && bootstrap.previous_epoch + 1 == bootstrap.epoch
            && bootstrap.previous_finalized_height + 1 == bootstrap.activation_height;
        return bootstrap.protocol_version == ProtocolVersion && !bootstrap.network_id.is_zero()
               && (normal_transition || recovery_transition) && bootstrap.activation_height != 0
               && bootstrap.first_dag_section != 0 && !bootstrap.previous_section_root.empty()
               && !bootstrap.previous_state_commitment.empty()
               && !bootstrap.previous_decision_certificate_hash.empty() && !bootstrap.epoch_change_hash.empty()
               && next_validators.protocol_version == ProtocolVersion
               && next_validators.network_id == bootstrap.network_id && next_validators.epoch == bootstrap.epoch
               && bootstrap.validator_set_hash == hash_validator_set(next_validators);
    }

    std::string trust_anchor_action_hash(const TrustAnchorV1& anchor) {
        return unsigned_trust_anchor_hash(anchor);
    }

    std::string hash_trust_anchor(const TrustAnchorV1& anchor) {
        return domain_hash(TrustAnchorDomain, anchor);
    }

    bool verify_trust_anchor(const TrustAnchorV1& anchor) {
        const auto validators = ValidatorSetView::create(anchor.initial_validators);
        return anchor.protocol_version == ProtocolVersion && !anchor.network_id.is_zero()
               && anchor.initial_validators.network_id == anchor.network_id && validators.has_value()
               && anchor.governance_policy.network_id == anchor.network_id
               && anchor.recovery_policy.network_id == anchor.network_id
               && verify_multisig_policy(anchor.governance_policy)
               && verify_multisig_policy(anchor.recovery_policy)
               && anchor.governance_policy.public_keys.size() == GovernanceSignerCount
               && anchor.governance_policy.threshold == GovernanceThreshold
               && anchor.recovery_policy.public_keys.size() == GovernanceSignerCount
               && anchor.recovery_policy.threshold == RecoveryThreshold
               && anchor.minimum_recovery_delay_ms == MinimumRecoveryDelayMillis
               && anchor.authorization.action_hash == unsigned_trust_anchor_hash(anchor)
               && verify_authorization(anchor.governance_policy, anchor.authorization, 1);
    }

    std::string recovery_action_hash(const RecoveryDocumentV2& recovery) {
        return unsigned_recovery_hash(recovery);
    }

    bool verify_recovery_document(const RecoveryDocumentV2& recovery,
                                  const MultisigPolicy&     recovery_policy,
                                  const ValidatorSet&       current_validators,
                                  const ValidatorSet&       next_validators,
                                  std::uint64_t             minimum_sequence,
                                  std::uint64_t             expected_finalized_height,
                                  std::string_view          expected_header_hash,
                                  std::string_view          expected_state_commitment) {
        const auto next = ValidatorSetView::create_recovery_transition(next_validators, recovery.operators);
        return recovery.protocol_version == ProtocolVersion && recovery.network_id == recovery_policy.network_id
               && recovery_policy.public_keys.size() == GovernanceSignerCount
               && recovery_policy.threshold == RecoveryThreshold
               && current_validators.protocol_version == ProtocolVersion && !current_validators.validators.empty()
               && current_validators.network_id == recovery.network_id
               && current_validators.epoch == recovery.current_epoch
               && recovery.current_epoch != std::numeric_limits<std::uint64_t>::max()
               && recovery.activation_epoch == recovery.current_epoch + 1
               && recovery.current_validator_set_hash == hash_validator_set(current_validators) && next.has_value()
               && next_validators.network_id == recovery.network_id
               && next_validators.epoch == recovery.activation_epoch
               && recovery.next_validator_set_hash == next.value().hash()
               && recovery.finalized_height == expected_finalized_height
               && recovery.finalized_header_hash == expected_header_hash
               && recovery.finalized_state_commitment == expected_state_commitment
               && !recovery.finalized_header_hash.empty() && !recovery.finalized_state_commitment.empty()
               && recovery.finalized_height != std::numeric_limits<std::uint64_t>::max()
               && !recovery.registry_document_hash.empty() && recovery.signed_at_ms != 0
               && recovery.signed_at_ms <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
               && recovery.activate_after_ms
                      <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
               && recovery.activate_after_ms >= recovery.signed_at_ms
               && recovery.activate_after_ms - recovery.signed_at_ms >= MinimumRecoveryDelayMillis
               && recovery.authorization.sequence == recovery.recovery_sequence
               && recovery.recovery_sequence >= minimum_sequence
               && recovery.authorization.action_hash == unsigned_recovery_hash(recovery)
               && verify_authorization(recovery_policy, recovery.authorization, minimum_sequence);
    }

    std::string activation_action_hash(const ActivationManifestV1& activation) {
        return unsigned_activation_hash(activation);
    }

    bool verify_activation_manifest(const ActivationManifestV1& activation,
                                    const MultisigPolicy&       policy,
                                    std::uint64_t               current_height,
                                    std::uint64_t               minimum_sequence) {
        return activation.protocol_version == ProtocolVersion && activation.network_id == policy.network_id
               && policy.public_keys.size() == GovernanceSignerCount && policy.threshold == GovernanceThreshold
               && activation.activation_height > current_height && activation.activation_dag_section != 0
               && activation.activation_dag_section % ShadowSectionInterval == 0
               && !activation.validator_set_hash.empty() && activation.require_intent_v2
               && activation.authorization.action_hash == unsigned_activation_hash(activation)
               && verify_authorization(policy, activation.authorization, minimum_sequence);
    }

    std::string hash_state_commitment(const StateCommitmentV2& commitment) {
        return domain_hash(StateDomain, commitment);
    }

    std::string segmented_state_root(std::string_view                                        domain,
                                     const std::vector<std::pair<std::string, std::string>>& entries) {
        if (domain.empty()) {
            return {};
        }
        std::array<std::vector<std::pair<std::string, std::string>>, 256> buckets;
        for (const auto& [key, value] : entries) {
            if (key.empty()) {
                return {};
            }
            const auto   key_hash = domain_hash(StateKeyDomain, std::tuple { std::string(domain), key });
            unsigned int bucket   = 0;
            const auto   parsed   = std::from_chars(key_hash.data(), key_hash.data() + 2, bucket, 16);
            if (parsed.ec != std::errc {} || parsed.ptr != key_hash.data() + 2) {
                return {};
            }
            buckets[bucket].emplace_back(key, value);
        }

        std::vector<std::string> bucket_roots;
        bucket_roots.reserve(buckets.size());
        for (std::size_t index = 0; index < buckets.size(); ++index) {
            auto& bucket = buckets[index];
            std::ranges::sort(bucket);
            const auto duplicate =
                std::adjacent_find(bucket.begin(), bucket.end(), [](const auto& left, const auto& right) {
                    return left.first == right.first;
                });
            if (duplicate != bucket.end()) {
                return {};
            }
            std::vector<std::string> leaves;
            leaves.reserve(bucket.size());
            for (const auto& [key, value] : bucket) {
                leaves.push_back(domain_hash(StateLeafDomain, std::tuple { std::string(domain), key, value }));
            }
            bucket_roots.push_back(
                domain_hash(StateBucketDomain, std::tuple { std::string(domain), index, leaves }));
        }
        return domain_hash(StateRootDomain, std::tuple { std::string(domain), bucket_roots });
    }

} // namespace ExtraChain::Consensus
