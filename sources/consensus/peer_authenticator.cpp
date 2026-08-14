/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/peer_authenticator.h"

#include <algorithm>

#include "utils/exc_utils.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::size_t MaximumOutstandingChallenges = 128;
    }

    PeerAuthenticator::PeerAuthenticator(const ValidatorSetView&          validators,
                                         std::optional<ValidatorIdentity> identity)
        : validators_(validators)
        , identity_(std::move(identity)) {
    }

    std::expected<AuthenticationChallenge, ConsensusError> PeerAuthenticator::create_challenge(
        std::string               challenger_node_identifier,
        std::string               target_node_identifier,
        std::chrono::milliseconds lifetime) {
        if (challenger_node_identifier.empty() || target_node_identifier.empty()
            || lifetime <= std::chrono::milliseconds::zero() || lifetime > std::chrono::minutes(1)) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        const auto target_is_validator = std::ranges::any_of(validators_.active(), [&](const auto& validator) {
            return validator.node_identifier == target_node_identifier;
        });
        if (!target_is_validator) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        std::lock_guard lock(mutex_);
        const auto      now = now_millis();
        remove_expired(now);
        if (outstanding_.size() >= MaximumOutstandingChallenges) {
            return std::unexpected(ConsensusError::NotReady);
        }
        AuthenticationChallenge challenge {
            .protocol_version           = ProtocolVersion,
            .network_id                 = validators_.document().network_id,
            .epoch                      = validators_.document().epoch,
            .challenge_id               = Utils::generate_random_hex(32),
            .challenger_node_identifier = std::move(challenger_node_identifier),
            .target_node_identifier     = std::move(target_node_identifier),
            .expires_at_millis          = now + static_cast<std::uint64_t>(lifetime.count()),
        };
        outstanding_.insert_or_assign(hash_authentication_challenge(challenge), challenge);
        return challenge;
    }

    std::expected<AuthenticationResponse, ConsensusError> PeerAuthenticator::answer_challenge(
        const AuthenticationChallenge& challenge,
        std::string_view               local_node_identifier) const {
        if (!identity_.has_value() || challenge.protocol_version != ProtocolVersion
            || challenge.network_id != validators_.document().network_id
            || challenge.epoch != validators_.document().epoch
            || challenge.target_node_identifier != local_node_identifier
            || challenge.challenger_node_identifier.empty() || challenge.challenge_id.empty()
            || challenge.expires_at_millis < now_millis()) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        const auto* validator = validators_.find(identity_.value().validator_id);
        if (validator == nullptr || validator->node_identifier != local_node_identifier) {
            return std::unexpected(ConsensusError::InvalidValidator);
        }
        AuthenticationResponse response {
            .protocol_version = ProtocolVersion,
            .network_id       = challenge.network_id,
            .epoch            = challenge.epoch,
            .challenge_hash   = hash_authentication_challenge(challenge),
            .validator_id     = identity_.value().validator_id,
            .node_identifier  = std::string(local_node_identifier),
        };
        const auto signature =
            sign_payload(identity_.value().key, authentication_response_signing_payload(response));
        if (!signature.has_value()) {
            return std::unexpected(signature.error());
        }
        response.signature = signature.value();
        return response;
    }

    std::expected<std::string, ConsensusError> PeerAuthenticator::verify_response(
        const AuthenticationResponse& response,
        std::string_view              peer_identifier) {
        std::lock_guard lock(mutex_);
        const auto      now = now_millis();
        remove_expired(now);
        const auto  challenge = outstanding_.find(response.challenge_hash);
        const auto* validator = validators_.find(response.validator_id);
        if (response.protocol_version != ProtocolVersion
            || response.network_id != validators_.document().network_id
            || response.epoch != validators_.document().epoch || challenge == outstanding_.end()
            || challenge->second.expires_at_millis < now
            || challenge->second.target_node_identifier != peer_identifier || validator == nullptr
            || validator->node_identifier != response.node_identifier
            || response.node_identifier != peer_identifier
            || !verify_payload(validator->consensus_public_key,
                               authentication_response_signing_payload(response),
                               response.signature)) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        const auto validator_id = response.validator_id;
        outstanding_.erase(challenge);
        authenticated_.insert_or_assign(std::string(peer_identifier), validator_id);
        return validator_id;
    }

    bool PeerAuthenticator::is_authenticated(std::string_view peer_identifier,
                                             std::string_view validator_id) const {
        std::lock_guard lock(mutex_);
        const auto      found = authenticated_.find(std::string(peer_identifier));
        return found != authenticated_.end() && found->second == validator_id;
    }

    std::optional<std::string> PeerAuthenticator::authenticated_validator(std::string_view peer_identifier) const {
        std::lock_guard lock(mutex_);
        const auto      found = authenticated_.find(std::string(peer_identifier));
        return found == authenticated_.end() ? std::nullopt : std::optional<std::string>(found->second);
    }

    void PeerAuthenticator::forget_peer(std::string_view peer_identifier) {
        std::lock_guard lock(mutex_);
        authenticated_.erase(std::string(peer_identifier));
    }

    std::uint64_t PeerAuthenticator::now_millis() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count());
    }

    void PeerAuthenticator::remove_expired(std::uint64_t now) {
        std::erase_if(outstanding_, [now](const auto& item) {
            return item.second.expires_at_millis < now;
        });
    }

} // namespace ExtraChain::Consensus
