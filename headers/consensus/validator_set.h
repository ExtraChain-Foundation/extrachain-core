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
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include "consensus/consensus_types.h"
#include "consensus/consensus_protocol.h"

namespace ExtraChain::Consensus {

    class EXTRACHAIN_EXPORT ValidatorSetView {
    public:
        static std::expected<ValidatorSetView, ConsensusError> create(ValidatorSet validators);
        static std::expected<ValidatorSetView, ConsensusError> create_governed(
            ValidatorSet                            validators,
            std::string                             registry_document_hash,
            const std::vector<OperatorAttestation>& operators,
            const MultisigPolicy&                   policy,
            const GovernanceAuthorization&          authorization,
            std::uint64_t                           minimum_sequence);
        static std::expected<ValidatorSetView, ConsensusError> create_epoch_transition(
            ValidatorSet          validators,
            const EpochChangeV1&  change,
            const MultisigPolicy& policy,
            std::uint64_t         current_height,
            std::uint64_t         minimum_sequence);
        static std::expected<ValidatorSetView, ConsensusError> create_recovery_transition(
            ValidatorSet                            validators,
            const std::vector<OperatorAttestation>& operators);

        [[nodiscard]] const ValidatorSet&                 document() const noexcept;
        [[nodiscard]] const std::vector<ValidatorRecord>& active() const noexcept;
        [[nodiscard]] const ValidatorRecord*              find(std::string_view validator_id) const noexcept;
        [[nodiscard]] std::optional<std::size_t>          index_of(std::string_view validator_id) const noexcept;
        [[nodiscard]] const ValidatorRecord&              leader(std::uint64_t height, std::uint64_t round) const;
        [[nodiscard]] std::size_t                         quorum() const noexcept;
        [[nodiscard]] std::size_t                         fault_limit() const noexcept;
        [[nodiscard]] const std::string&                  hash() const noexcept;

    private:
        ValidatorSetView(ValidatorSet validators, std::vector<ValidatorRecord> active, std::string hash);

        ValidatorSet                 validators_;
        std::vector<ValidatorRecord> active_;
        std::string                  hash_;
    };

    EXTRACHAIN_EXPORT std::expected<ValidatorRecord, ConsensusError> make_validator_record(
        const ActorId&           network_id,
        std::uint64_t            epoch,
        const Actor<KeyPrivate>& actor,
        const KeyPrivate&        consensus_key,
        std::string              node_identifier,
        std::uint64_t            valid_from,
        std::uint64_t            valid_until = 0);

    EXTRACHAIN_EXPORT std::expected<ValidatorSet, ConsensusError> make_validator_set(
        const ActorId&               network_id,
        std::uint64_t                epoch,
        std::vector<ValidatorRecord> validators,
        const Actor<KeyPrivate>&     governance_actor);

    EXTRACHAIN_EXPORT std::string governed_validator_set_action_hash(
        const ValidatorSet&                     validators,
        std::string_view                        registry_document_hash,
        const std::vector<OperatorAttestation>& operators);
    EXTRACHAIN_EXPORT QuorumCertificate make_genesis_certificate(const ValidatorSetView& validators);
    EXTRACHAIN_EXPORT QuorumCertificate make_epoch_genesis_certificate(const ValidatorSetView& validators,
                                                                       const EpochBootstrapV1& bootstrap);

} // namespace ExtraChain::Consensus
