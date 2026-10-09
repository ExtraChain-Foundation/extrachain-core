#include "consensus/mining_epoch.h"
#include "consensus/light_client.h"

#include "utils/exc_utils.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        // Recorded sections stay far below this, so the offsets added to them cannot overflow.
        constexpr std::uint64_t MaximumSection = std::numeric_limits<std::int64_t>::max();
    } // namespace

    std::optional<std::uint64_t> mining_window_opening_section(const MiningEpochState& state) {
        if (state.challenge_section == 0 || state.challenge_section > MaximumSection)
            return std::nullopt;
        return state.challenge_section + MiningFinalitySections + 1;
    }

    std::optional<std::uint64_t> mining_settlement_section(const MiningEpochState& state) {
        if (state.proof_last_section == 0 || state.proof_last_section > MaximumSection)
            return std::nullopt;
        return state.proof_last_section + MiningFinalitySections + 1;
    }

    bool mining_window_accepts(const MiningEpochState& state, std::uint64_t section) {
        return !state.settled && state.challenge.has_value() && state.proof_first_section != 0
               && section >= state.proof_first_section
               && (state.proof_last_section == 0 || section <= state.proof_last_section);
    }

    std::expected<std::uint64_t, ConsensusError> reserve_mining_emission(std::uint64_t reserved_units,
                                                                         std::uint64_t epoch_units) {
        if (reserved_units > MaximumMiningEmissionUnits
            || epoch_units > MaximumMiningEmissionUnits - reserved_units)
            return std::unexpected(ConsensusError::InvalidIntent);
        return reserved_units + epoch_units;
    }

    std::expected<MiningEpochState, ConsensusError> freeze_mining_epoch(
        const ActorId&                         network,
        std::uint64_t                          epoch,
        std::uint64_t                          budget_units,
        const std::vector<MiningRegistration>& registrations,
        std::uint64_t                          first_section) {
        if (network.is_zero() || epoch > MaximumMiningEpoch || budget_units > MaximumMiningEmissionUnits
            || registrations.size() > MaximumMiningRegistrations || first_section == 0
            || first_section > MaximumSection)
            return std::unexpected(ConsensusError::InvalidIntent);
        MiningEpochState result { .network = network, .epoch = epoch, .budget_units = budget_units };
        std::uint64_t    total_bytes = 0;
        for (const auto& registration : registrations) {
            const auto identity = storage_dataset_id(network, registration.dataset);
            if (!identity.has_value() || registration.provider.is_zero() || registration.section >= first_section)
                return std::unexpected(ConsensusError::InvalidIntent);
            auto [dataset, inserted] =
                result.datasets.try_emplace(identity.value(),
                                            MiningDatasetBudget { .dataset = registration.dataset });
            if (inserted) {
                if (result.datasets.size() > MaximumMiningDatasets)
                    return std::unexpected(ConsensusError::DataTooLarge);
                total_bytes += registration.dataset.bytes;
            }
            dataset->second.providers.insert(registration.provider.to_string());
        }
        for (auto& [identity, dataset] : result.datasets) {
            const boost::multiprecision::uint128_t weighted =
                boost::multiprecision::uint128_t(budget_units) * dataset.dataset.bytes;
            dataset.units = static_cast<std::uint64_t>(weighted / total_bytes);
        }
        return result;
    }

    std::expected<void, ConsensusError> open_mining_proof_window(MiningEpochState&       state,
                                                                 const StorageChallenge& challenge,
                                                                 std::uint64_t           first_section,
                                                                 std::uint64_t           closes_ms) {
        if (state.challenge.has_value() || state.settled)
            return std::unexpected(ConsensusError::Replay);
        const auto opening = mining_window_opening_section(state);
        if (!opening.has_value() || challenge.epoch != state.epoch || first_section != opening.value()
            || closes_ms == 0 || challenge.checkpoint.size() != 64
            || !std::ranges::all_of(challenge.checkpoint, [](char value) {
                   return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
               }))
            return std::unexpected(ConsensusError::InvalidProof);
        state.challenge           = challenge;
        state.proof_first_section = first_section;
        state.proof_closes_ms     = closes_ms;
        return { };
    }

    std::expected<void, ConsensusError> open_mining_proof_window(MiningEpochState&          state,
                                                                 const FinalityProof&       checkpoint,
                                                                 const LightClientVerifier& verifier,
                                                                 std::uint64_t              closes_ms) {
        const auto opening = mining_window_opening_section(state);
        if (!opening.has_value() || checkpoint.finalized_proposal.header.network_id != state.network
            || checkpoint.finalized_proposal.header.dag_section != state.challenge_section
            || checkpoint.child_proposal.header.dag_section != state.challenge_section + ShadowSectionInterval
            || checkpoint.grandchild_proposal.header.dag_section != opening.value() - 1
            || !verifier.verify_finality_proof(checkpoint))
            return std::unexpected(ConsensusError::InvalidProof);
        return open_mining_proof_window(state,
                                        StorageChallenge { .epoch = state.epoch,
                                                           .checkpoint =
                                                               hash_header(checkpoint.finalized_proposal.header) },
                                        opening.value(),
                                        closes_ms);
    }

    std::expected<void, ConsensusError> settle_mining_epoch(MiningEpochState&          state,
                                                            const FinalityProof&       checkpoint,
                                                            const LightClientVerifier& verifier) {
        const auto settlement = mining_settlement_section(state);
        if (!settlement.has_value() || checkpoint.finalized_proposal.header.network_id != state.network
            || checkpoint.finalized_proposal.header.dag_section != state.proof_last_section
            || checkpoint.child_proposal.header.dag_section != state.proof_last_section + ShadowSectionInterval
            || checkpoint.grandchild_proposal.header.dag_section != settlement.value() - 1
            || !verifier.verify_finality_proof(checkpoint))
            return std::unexpected(ConsensusError::InvalidProof);
        return settle_mining_epoch(state, checkpoint.finalized_proposal.header.dag_section);
    }

    std::expected<void, ConsensusError> accept_mining_proof(MiningEpochState&   state,
                                                            const ActorId&      provider,
                                                            const std::string&  dataset_id,
                                                            std::uint64_t       section,
                                                            const StorageProof& proof) {
        if (!mining_window_accepts(state, section))
            return std::unexpected(ConsensusError::InvalidHeight);
        auto       dataset = state.datasets.find(dataset_id);
        const auto actor   = provider.to_string();
        if (dataset == state.datasets.end() || !dataset->second.providers.contains(actor))
            return std::unexpected(ConsensusError::InvalidIntent);
        if (dataset->second.accepted.contains(actor))
            return std::unexpected(ConsensusError::Replay);
        if (!verify_storage_proof(state.network,
                                  provider,
                                  dataset->second.dataset,
                                  state.challenge.value(),
                                  proof))
            return std::unexpected(ConsensusError::InvalidProof);
        dataset->second.accepted.insert(actor);
        return { };
    }

    std::expected<void, ConsensusError> settle_mining_epoch(MiningEpochState& state,
                                                            std::uint64_t     finalized_section) {
        if (state.settled)
            return std::unexpected(ConsensusError::Replay);
        if (!state.claimed.empty() || !state.rewards.empty())
            return std::unexpected(ConsensusError::InvalidIntent);
        if (!state.challenge.has_value() || state.proof_last_section == 0
            || finalized_section < state.proof_last_section)
            return std::unexpected(ConsensusError::InvalidHeight);
        std::map<std::string, std::uint64_t> rewards;
        std::uint64_t                        paid = 0;
        for (const auto& [identity, dataset] : state.datasets) {
            if (dataset.accepted.empty())
                continue;
            const auto amount = dataset.units / dataset.accepted.size();
            for (const auto& provider : dataset.accepted) {
                if (!dataset.providers.contains(provider) || paid > state.budget_units
                    || amount > state.budget_units - paid)
                    return std::unexpected(ConsensusError::InvalidIntent);
                if (amount != 0)
                    rewards[provider] += amount;
                paid += amount;
            }
        }
        state.rewards = std::move(rewards);
        state.settled = true;
        return { };
    }

    std::string mining_epoch_root(const MiningEpochState& state) {
        return Utils::calculate_hash("EXC_MINING_EPOCH_V1" + MessagePack::serialize(state),
                                     Utils::HashAlgorithm::Blake3);
    }
} // namespace ExtraChain::Consensus
