#include "consensus/mining_replay.h"

#include "chain/dag.h"
#include "utils/exc_utils.h"

namespace ExtraChain::Consensus {
    std::expected<MiningState, ConsensusError> configure_mining_state(
        const ActorId&                             network,
        std::uint64_t                              boundary,
        const std::optional<MiningEmissionPolicy>& policy) {
        auto state = create_mining_state(network, boundary);
        if (!state.has_value())
            return std::unexpected(state.error());
        if (policy.has_value()
            && (!mining_policy_total(policy.value()).has_value()
                || policy.value().first_epoch < boundary / ShadowSectionInterval))
            return std::unexpected(ConsensusError::InvalidGovernance);
        state.value().emission_policy_hash = mining_policy_hash(policy);
        return state.value();
    }

    std::expected<MiningState, ConsensusError> replay_mining_batch(
        MiningState                                state,
        const std::optional<MiningEmissionPolicy>& policy,
        const SectionBatchData&                    batch,
        const MiningFinalityReader&                read_finality,
        const LightClientVerifier&                 verifier) {
        return replay_mining_batch(std::move(state), policy, batch, read_finality, verifier, nullptr);
    }

    std::expected<MiningState, ConsensusError> replay_mining_batch(
        MiningState                                state,
        const std::optional<MiningEmissionPolicy>& policy,
        const SectionBatchData&                    batch,
        const MiningFinalityReader&                read_finality,
        const LightClientVerifier&                 verifier,
        std::string*                               invalid_request) {
        if (invalid_request != nullptr)
            invalid_request->clear();
        if (state.emission_policy_hash != mining_policy_hash(policy) || state.section == UINT64_MAX
            || batch.manifest.first_section != state.section + 1
            || batch.manifest.last_section < batch.manifest.first_section
            || batch.manifest.last_section - batch.manifest.first_section >= ShadowSectionInterval
            || batch.sections.size() != batch.manifest.last_section - batch.manifest.first_section + 1)
            return std::unexpected(ConsensusError::InvalidHeight);
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        for (const auto& [section, bytes] : batch.sections) {
            if (section != state.section + 1)
                return std::unexpected(ConsensusError::InvalidHeight);
            const auto decoded = Json::deserialize<Section>(bytes);
            if (!decoded.has_value())
                return std::unexpected(ConsensusError::InvalidIntent);
            std::uint64_t budget = 0;
            if (policy.has_value() && (section - 1) % ShadowSectionInterval == 0) {
                const auto scheduled = mining_policy_budget(policy.value(), (section - 1) / ShadowSectionInterval);
                if (!scheduled.has_value())
                    return std::unexpected(scheduled.error());
                budget = scheduled.value();
            }
            const auto payouts = advance_mining_state(state, section, budget, read_finality, verifier);
            if (!payouts.has_value())
                return std::unexpected(payouts.error());
            bool settled = false;
            for (const auto& transaction : decoded.value().transactions) {
                if (transaction.section() != SectionId(section))
                    return std::unexpected(ConsensusError::InvalidHeight);
                if (transaction.type() == TransactionType::MiningSettlement) {
                    const auto verified =
                        verify_mining_settlement_transaction(transaction, state.network, verifier);
                    if (settled || payouts.value().empty() || !verified.has_value()
                        || verified.value() != payouts.value())
                        return std::unexpected(ConsensusError::InvalidProof);
                    settled = true;
                } else if (is_mining_request(transaction.type())) {
                    const auto envelope = intent_from_transaction(transaction);
                    if (!envelope.has_value())
                        return std::unexpected(envelope.error());
                    const auto applied = policy.has_value()
                                             ? apply_mining_request(state, envelope.value())
                                             : std::expected<void, ConsensusError> { std::unexpected(
                                                   ConsensusError::InvalidGovernance) };
                    if (!applied.has_value()) {
                        if (invalid_request != nullptr)
                            *invalid_request = hash_intent(envelope.value().intent);
                        return std::unexpected(applied.error());
                    }
                }
            }
            if (settled != !payouts.value().empty())
                return std::unexpected(ConsensusError::InvalidProof);
        }
        return state;
    }
} // namespace ExtraChain::Consensus
