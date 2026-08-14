/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/validator_set.h"

#include <algorithm>
#include <set>

#include "core/byte_array.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"

namespace ExtraChain::Consensus {
    namespace {
        std::optional<ActorId> actor_id_for(std::string_view encoded_public_key) {
            const auto decoded = ByteArray::fromBase64(encoded_public_key);
            if (!decoded.has_value() || decoded.value().size() != crypto_sign_PUBLICKEYBYTES) {
                return std::nullopt;
            }
            const auto hash     = Utils::calculate_hash(decoded.value().toString(), Utils::HashAlgorithm::Blake3);
            const auto actor_id = ActorId::create(hash.substr(0, ActorId::SIZE));
            return actor_id.has_value() ? std::optional<ActorId>(actor_id.value()) : std::nullopt;
        }

        bool valid_record(const ValidatorRecord& record, const ActorId& network_id, std::uint64_t epoch) {
            const auto consensus_key = ByteArray::fromBase64(record.consensus_public_key);
            const auto actor_id      = actor_id_for(record.actor_public_key);
            return record.protocol_version == ProtocolVersion && record.network_id == network_id
                   && record.epoch == epoch && !record.validator_id.empty() && actor_id.has_value()
                   && actor_id.value() == record.actor_id && consensus_key.has_value()
                   && consensus_key.value().size() == crypto_sign_PUBLICKEYBYTES
                   && record.validator_id
                          == validator_id_for(consensus_key.value().toArray<crypto_sign_PUBLICKEYBYTES>())
                   && !record.node_identifier.empty() && record.node_identifier.size() <= 128
                   && (record.valid_until == 0 || record.valid_until > record.valid_from)
                   && verify_payload(record.actor_public_key,
                                     validator_binding_payload(record),
                                     record.actor_signature)
                   && verify_payload(record.consensus_public_key,
                                     validator_binding_payload(record),
                                     record.consensus_signature);
        }
    } // namespace

    ValidatorSetView::ValidatorSetView(ValidatorSet                 validators,
                                       std::vector<ValidatorRecord> active,
                                       std::string                  hash)
        : validators_(std::move(validators))
        , active_(std::move(active))
        , hash_(std::move(hash)) {
    }

    std::expected<ValidatorSetView, ConsensusError> ValidatorSetView::create(ValidatorSet validators) {
        if (validators.protocol_version != ProtocolVersion || validators.network_id.is_zero()
            || validators.epoch == 0 || validators.validators.empty()) {
            return std::unexpected(ConsensusError::InvalidValidatorSet);
        }

        const auto governance_id = actor_id_for(validators.governance_public_key);
        if (!governance_id.has_value() || governance_id.value() != validators.network_id
            || !verify_payload(validators.governance_public_key,
                               validator_set_signing_payload(validators),
                               validators.governance_signature)) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }

        std::set<std::string>        validator_ids;
        std::set<ActorId>            actor_ids;
        std::set<std::string>        node_identifiers;
        std::set<std::string>        consensus_keys;
        std::vector<ValidatorRecord> active;
        for (const auto& validator : validators.validators) {
            if (!valid_record(validator, validators.network_id, validators.epoch)
                || !validator_ids.insert(validator.validator_id).second
                || !consensus_keys.insert(validator.consensus_public_key).second) {
                return std::unexpected(ConsensusError::InvalidValidatorSet);
            }
            if (validator.status != ValidatorStatus::Active) {
                continue;
            }
            if (validator.valid_from != 0 || validator.valid_until != 0) {
                return std::unexpected(ConsensusError::InvalidValidatorSet);
            }
            if (!actor_ids.insert(validator.actor_id).second
                || !node_identifiers.insert(validator.node_identifier).second) {
                return std::unexpected(ConsensusError::InvalidValidatorSet);
            }
            active.push_back(validator);
        }

        if (active.size() < 4) {
            return std::unexpected(ConsensusError::InvalidValidatorSet);
        }
        std::ranges::sort(active, {}, &ValidatorRecord::validator_id);
        std::ranges::sort(validators.validators, {}, &ValidatorRecord::validator_id);
        const auto validator_set_hash = hash_validator_set(validators);
        return ValidatorSetView(std::move(validators), std::move(active), validator_set_hash);
    }

    const ValidatorSet& ValidatorSetView::document() const noexcept {
        return validators_;
    }

    const std::vector<ValidatorRecord>& ValidatorSetView::active() const noexcept {
        return active_;
    }

    const ValidatorRecord* ValidatorSetView::find(std::string_view validator_id) const noexcept {
        const auto found = std::ranges::lower_bound(active_, validator_id, {}, &ValidatorRecord::validator_id);
        return found != active_.end() && found->validator_id == validator_id ? &*found : nullptr;
    }

    std::optional<std::size_t> ValidatorSetView::index_of(std::string_view validator_id) const noexcept {
        const auto found = std::ranges::lower_bound(active_, validator_id, {}, &ValidatorRecord::validator_id);
        if (found == active_.end() || found->validator_id != validator_id) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(std::distance(active_.begin(), found));
    }

    const ValidatorRecord& ValidatorSetView::leader(std::uint64_t height, std::uint64_t round) const {
        return active_.at(static_cast<std::size_t>((height + round) % active_.size()));
    }

    std::size_t ValidatorSetView::quorum() const noexcept {
        return (2 * active_.size()) / 3 + 1;
    }

    std::size_t ValidatorSetView::fault_limit() const noexcept {
        return (active_.size() - 1) / 3;
    }

    const std::string& ValidatorSetView::hash() const noexcept {
        return hash_;
    }

    std::expected<ValidatorRecord, ConsensusError> make_validator_record(const ActorId&           network_id,
                                                                         std::uint64_t            epoch,
                                                                         const Actor<KeyPrivate>& actor,
                                                                         const KeyPrivate&        consensus_key,
                                                                         std::string              node_identifier,
                                                                         std::uint64_t            valid_from,
                                                                         std::uint64_t            valid_until) {
        if (network_id.is_zero() || epoch == 0 || actor.empty() || consensus_key.empty()
            || node_identifier.empty()) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        ValidatorRecord record {
            .protocol_version     = ProtocolVersion,
            .network_id           = network_id,
            .epoch                = epoch,
            .validator_id         = validator_id_for(consensus_key.public_key()),
            .actor_id             = actor.id(),
            .actor_public_key     = Utils::to_base64(actor.key().public_key()),
            .consensus_public_key = Utils::to_base64(consensus_key.public_key()),
            .node_identifier      = std::move(node_identifier),
            .valid_from           = valid_from,
            .valid_until          = valid_until,
            .status               = ValidatorStatus::Active,
        };
        const auto payload             = validator_binding_payload(record);
        const auto actor_signature     = sign_payload(actor.key(), payload);
        const auto consensus_signature = sign_payload(consensus_key, payload);
        if (!actor_signature.has_value() || !consensus_signature.has_value()) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        record.actor_signature     = actor_signature.value();
        record.consensus_signature = consensus_signature.value();
        return record;
    }

    std::expected<ValidatorSet, ConsensusError> make_validator_set(const ActorId&               network_id,
                                                                   std::uint64_t                epoch,
                                                                   std::vector<ValidatorRecord> validators,
                                                                   const Actor<KeyPrivate>&     governance_actor) {
        if (network_id.is_zero() || governance_actor.id() != network_id || epoch == 0) {
            return std::unexpected(ConsensusError::InvalidValidatorSet);
        }
        std::ranges::sort(validators, {}, &ValidatorRecord::validator_id);
        ValidatorSet result {
            .protocol_version      = ProtocolVersion,
            .network_id            = network_id,
            .epoch                 = epoch,
            .validators            = std::move(validators),
            .governance_public_key = Utils::to_base64(governance_actor.key().public_key()),
        };
        const auto signature = sign_payload(governance_actor.key(), validator_set_signing_payload(result));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        result.governance_signature = signature.value();
        return result;
    }

} // namespace ExtraChain::Consensus
