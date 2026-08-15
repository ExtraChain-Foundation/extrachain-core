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

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "consensus/validator_set.h"

namespace ExtraChain::Consensus {

    enum class BootstrapFreshness : std::uint8_t {
        Unknown,
        Confirmed
    };

    struct PeerFreshnessObservation {
        std::string   peer_identifier;
        std::uint64_t epoch  = 0;
        std::uint64_t height = 0;
        std::string   header_hash;
    };

    struct LightClientStateV1 {
        std::uint16_t                    protocol_version = ProtocolVersion;
        ActorId                          network_id;
        ValidatorSet                     active_validators;
        std::optional<EpochBootstrapV1>  active_bootstrap;
        std::optional<EpochTransitionV1> pending_epoch;
        std::optional<MultisigPolicy>    pending_policy;
        std::uint64_t                    pending_minimum_sequence = 0;
        std::uint64_t                    trusted_epoch            = 0;
        std::uint64_t                    trusted_height           = 0;
        std::string                      trusted_header_hash;
        std::optional<TrustAnchorV1>     trust_anchor;
        std::vector<EpochStartV1>        epoch_history;
        BootstrapFreshness               freshness = BootstrapFreshness::Unknown;

        MSGPACK_DEFINE(protocol_version,
                       network_id,
                       active_validators,
                       active_bootstrap,
                       pending_epoch,
                       pending_policy,
                       pending_minimum_sequence,
                       trusted_epoch,
                       trusted_height,
                       trusted_header_hash,
                       trust_anchor,
                       epoch_history,
                       freshness)
    };

    class EXTRACHAIN_EXPORT LightClientVerifier {
    public:
        static std::expected<LightClientVerifier, ConsensusError> create(
            ValidatorSet                    trusted_set,
            std::optional<EpochBootstrapV1> bootstrap = std::nullopt);
        static std::expected<LightClientVerifier, ConsensusError> bootstrap(TrustAnchorV1 anchor);
        static std::expected<LightClientVerifier, ConsensusError> restore(LightClientStateV1 state);
        static std::expected<LightClientVerifier, ConsensusError> load(const std::filesystem::path& path);

        [[nodiscard]] bool                  verify_finality_proof(const FinalityProof& proof) const;
        std::expected<void, ConsensusError> advance(const FinalityProof& proof);
        [[nodiscard]] bool verify_transaction_proof(const TransactionInclusionProofV1& proof) const;
        std::expected<void, ConsensusError>                  schedule_epoch(const EpochChangeV1&               change,
                                                                            ValidatorSet                       next_set,
                                                                            const MultisigPolicy&              policy,
                                                                            const TransactionInclusionProofV1& proof,
                                                                            std::uint64_t                      minimum_sequence);
        [[nodiscard]] const ValidatorSetView&                active_validators() const noexcept;
        [[nodiscard]] const std::optional<ValidatorSetView>& pending_validators() const noexcept;
        [[nodiscard]] LightClientStateV1                     snapshot() const;
        std::expected<void, ConsensusError>                  save(const std::filesystem::path& path) const;
        [[nodiscard]] std::uint64_t                          trusted_height() const noexcept;
        std::expected<void, ConsensusError> apply_history_page(const BootstrapHistoryPageV1& page);
        std::expected<void, ConsensusError> confirm_freshness(
            const std::vector<PeerFreshnessObservation>& observations);
        [[nodiscard]] BootstrapFreshness freshness() const noexcept;

    private:
        friend class ShadowConsensus;

        explicit LightClientVerifier(ValidatorSetView trusted_set, std::optional<EpochBootstrapV1> bootstrap);
        std::expected<void, ConsensusError>   apply_history_page_in_place(const BootstrapHistoryPageV1& page);
        [[nodiscard]] const ValidatorSetView* validators_for(std::uint64_t epoch,
                                                             std::uint64_t height) const noexcept;
        [[nodiscard]] const EpochBootstrapV1* bootstrap_for(std::uint64_t epoch) const noexcept;

        ValidatorSetView                 active_;
        std::optional<EpochBootstrapV1>  active_bootstrap_;
        std::optional<ValidatorSetView>  pending_;
        std::optional<EpochChangeV1>     pending_change_;
        std::optional<EpochBootstrapV1>  pending_bootstrap_;
        std::optional<EpochTransitionV1> pending_transition_;
        std::optional<MultisigPolicy>    pending_policy_;
        std::uint64_t                    pending_minimum_sequence_ = 0;
        std::uint64_t                    trusted_epoch_            = 0;
        std::uint64_t                    trusted_height_           = 0;
        std::string                      trusted_header_hash_;
        std::optional<TrustAnchorV1>     trust_anchor_;
        std::vector<EpochStartV1>        epoch_history_;
        BootstrapFreshness               freshness_ = BootstrapFreshness::Unknown;
    };

} // namespace ExtraChain::Consensus

MSGPACK_ADD_ENUM(ExtraChain::Consensus::BootstrapFreshness)
