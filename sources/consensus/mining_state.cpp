#include "consensus/mining_state.h"

#include "utils/exc_utils.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    std::expected<MiningState, ConsensusError> create_mining_state(const ActorId& network,
                                                                   std::uint64_t  boundary) {
        if (network.is_zero() || boundary % ShadowSectionInterval != 0
            || !mining_epoch_schedule(boundary / ShadowSectionInterval).has_value())
            return std::unexpected(ConsensusError::InvalidHeight);
        return MiningState { .network = network, .section = boundary };
    }

    std::expected<void, ConsensusError> register_storage_provider(MiningState&          state,
                                                                  const ActorId&        provider,
                                                                  const StorageDataset& dataset) {
        const auto identity = storage_dataset_id(state.network, dataset);
        if (!identity.has_value() || provider.is_zero())
            return std::unexpected(ConsensusError::InvalidIntent);
        const auto actor    = provider.to_string();
        const auto existing = state.registrations.find(identity.value());
        if (existing != state.registrations.end() && existing->second.providers.contains(actor))
            return std::unexpected(ConsensusError::Replay);
        if (existing == state.registrations.end() && state.registrations.size() >= MaximumMiningDatasets)
            return std::unexpected(ConsensusError::DataTooLarge);
        std::size_t count = 0;
        for (const auto& [id, registration] : state.registrations)
            count += registration.providers.size();
        if (count >= MaximumMiningRegistrations)
            return std::unexpected(ConsensusError::DataTooLarge);
        auto [entry, inserted] =
            state.registrations.try_emplace(identity.value(), MiningRegisteredDataset { .dataset = dataset });
        entry->second.providers.emplace(actor, state.section);
        return { };
    }

    std::expected<void, ConsensusError> unregister_storage_provider(MiningState&       state,
                                                                    const ActorId&     provider,
                                                                    const std::string& dataset_id) {
        const auto entry = state.registrations.find(dataset_id);
        if (entry == state.registrations.end() || entry->second.providers.erase(provider.to_string()) == 0)
            return std::unexpected(ConsensusError::InvalidIntent);
        if (entry->second.providers.empty())
            state.registrations.erase(entry);
        return { };
    }

    std::expected<void, ConsensusError> submit_mining_proof(MiningState&        state,
                                                            std::uint64_t       epoch,
                                                            const ActorId&      provider,
                                                            const std::string&  dataset_id,
                                                            const StorageProof& proof) {
        const auto active = state.epochs.find(epoch);
        if (active == state.epochs.end())
            return std::unexpected(ConsensusError::InvalidEpoch);
        return accept_mining_proof(active->second, provider, dataset_id, state.section, proof);
    }

    std::expected<MiningPayouts, ConsensusError> advance_mining_state(MiningState&  state,
                                                                      std::uint64_t section,
                                                                      std::uint64_t epoch_budget_units,
                                                                      const MiningFinalityReader& read_finality,
                                                                      const LightClientVerifier&  verifier) {
        if (section == 0 || state.section != section - 1
            || !mining_epoch_schedule((section - 1) / ShadowSectionInterval).has_value())
            return std::unexpected(ConsensusError::InvalidHeight);
        if (state.minted_units > state.reserved_units || state.reserved_units > MaximumMiningEmissionUnits
            || ((section - 1) % ShadowSectionInterval != 0 && epoch_budget_units != 0))
            return std::unexpected(ConsensusError::InvalidIntent);
        if ((section - 1) % ShadowSectionInterval != 0) {
            state.section = section;
            return MiningPayouts { };
        }
        auto          next = state;
        MiningPayouts payouts;
        for (auto entry = next.epochs.begin(); entry != next.epochs.end();) {
            const auto schedule = mining_epoch_schedule(entry->first);
            if (!schedule.has_value())
                return std::unexpected(schedule.error());
            const bool opens  = schedule.value().proof_first_section == section;
            const bool closes = schedule.value().settlement_first_section == section;
            if (opens || closes) {
                if (!read_finality)
                    return std::unexpected(ConsensusError::DataUnavailable);
                const auto proof = read_finality(opens ? schedule.value().challenge_section
                                                       : schedule.value().proof_last_section);
                if (!proof.has_value())
                    return std::unexpected(proof.error());
                const auto changed = opens ? open_mining_proof_window(entry->second, proof.value(), verifier)
                                           : settle_mining_epoch(entry->second, proof.value(), verifier);
                if (!changed.has_value())
                    return std::unexpected(changed.error());
            }
            if (closes) {
                for (const auto& [provider, amount] : entry->second.rewards) {
                    if (amount > next.reserved_units - next.minted_units)
                        return std::unexpected(ConsensusError::InvalidIntent);
                    payouts[provider] += amount;
                    next.minted_units += amount;
                }
                entry = next.epochs.erase(entry);
            } else {
                ++entry;
            }
        }
        if ((section - 1) % ShadowSectionInterval == 0) {
            if (next.epochs.size() >= 8)
                return std::unexpected(ConsensusError::DataTooLarge);
            const auto reserved = reserve_mining_emission(next.reserved_units, epoch_budget_units);
            if (!reserved.has_value())
                return std::unexpected(reserved.error());
            std::vector<MiningRegistration> registrations;
            for (const auto& [id, dataset] : next.registrations)
                for (const auto& [provider, registered] : dataset.providers) {
                    const auto actor = ActorId::create(provider);
                    if (!actor.has_value() || registrations.size() >= MaximumMiningRegistrations)
                        return std::unexpected(ConsensusError::InvalidIntent);
                    registrations.push_back({ actor.value(), dataset.dataset, registered });
                }
            const auto epoch  = (section - 1) / ShadowSectionInterval;
            auto       frozen = freeze_mining_epoch(next.network, epoch, epoch_budget_units, registrations);
            if (!frozen.has_value())
                return std::unexpected(frozen.error());
            if (epoch_budget_units != 0 && !frozen.value().datasets.empty()
                && !next.epochs.emplace(epoch, std::move(frozen.value())).second)
                return std::unexpected(ConsensusError::Replay);
            next.reserved_units = reserved.value();
        }
        next.section = section;
        state        = std::move(next);
        return payouts;
    }

    std::string mining_state_root(const MiningState& state) {
        return Utils::calculate_hash("EXC_MINING_STATE_V1" + MessagePack::serialize(state),
                                     Utils::HashAlgorithm::Blake3);
    }
} // namespace ExtraChain::Consensus
