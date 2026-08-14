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

namespace ExtraChain::Consensus {

    class EXTRACHAIN_EXPORT ValidatorSetView {
    public:
        static std::expected<ValidatorSetView, ConsensusError> create(ValidatorSet validators);

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

} // namespace ExtraChain::Consensus
