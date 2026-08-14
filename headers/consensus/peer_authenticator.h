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

#include <chrono>
#include <expected>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "consensus/consensus_engine.h"

namespace ExtraChain::Consensus {

    class EXTRACHAIN_EXPORT PeerAuthenticator {
    public:
        explicit PeerAuthenticator(const ValidatorSetView&          validators,
                                   std::optional<ValidatorIdentity> identity = {});

        std::expected<AuthenticationChallenge, ConsensusError> create_challenge(
            std::string               challenger_node_identifier,
            std::string               target_node_identifier,
            std::chrono::milliseconds lifetime = std::chrono::seconds(15));
        std::expected<AuthenticationResponse, ConsensusError> answer_challenge(
            const AuthenticationChallenge& challenge,
            std::string_view               local_node_identifier) const;
        std::expected<std::string, ConsensusError> verify_response(const AuthenticationResponse& response,
                                                                   std::string_view              peer_identifier);
        [[nodiscard]] bool is_authenticated(std::string_view peer_identifier, std::string_view validator_id) const;
        [[nodiscard]] std::optional<std::string> authenticated_validator(std::string_view peer_identifier) const;
        void                                     forget_peer(std::string_view peer_identifier);

    private:
        [[nodiscard]] static std::uint64_t now_millis();
        void                               remove_expired(std::uint64_t now);

        const ValidatorSetView&                        validators_;
        std::optional<ValidatorIdentity>               identity_;
        std::map<std::string, AuthenticationChallenge> outstanding_;
        std::map<std::string, std::string>             authenticated_;
        mutable std::mutex                             mutex_;
    };

} // namespace ExtraChain::Consensus
