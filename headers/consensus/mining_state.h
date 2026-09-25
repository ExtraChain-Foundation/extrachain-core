#pragma once

#include "consensus/mining_epoch.h"

namespace ExtraChain::Consensus {
    struct MiningRegisteredDataset {
        StorageDataset                       dataset;
        std::map<std::string, std::uint64_t> providers;

        MSGPACK_DEFINE(dataset, providers)
    };

    struct MiningState {
        ActorId                                        network;
        std::uint64_t                                  section        = 0;
        std::uint64_t                                  reserved_units = 0;
        std::uint64_t                                  minted_units   = 0;
        std::string                                    emission_policy_hash;
        std::map<std::string, MiningRegisteredDataset> registrations;
        std::map<std::uint64_t, MiningEpochState>      epochs;

        MSGPACK_DEFINE(network, section, reserved_units, minted_units, emission_policy_hash, registrations, epochs)
    };

    using MiningFinalityReader = std::function<std::expected<FinalityProof, ConsensusError>(std::uint64_t)>;
    using MiningPayouts        = std::map<std::string, std::uint64_t>;

    struct MiningEpochWitness {
        MiningEpochState epoch;
        MerkleProof      membership;

        MSGPACK_DEFINE(epoch, membership)
    };

    std::expected<MiningEpochWitness, ConsensusError> make_mining_epoch_witness(const MiningState& state,
                                                                                std::uint64_t      epoch);
    bool verify_mining_epoch_witness(const MiningEpochWitness& witness, std::string_view state_root);

    std::expected<MiningState, ConsensusError> create_mining_state(const ActorId& network, std::uint64_t boundary);
    std::expected<void, ConsensusError>        register_storage_provider(MiningState&          state,
                                                                         const ActorId&        provider,
                                                                         const StorageDataset& dataset);
    std::expected<void, ConsensusError>        unregister_storage_provider(MiningState&       state,
                                                                           const ActorId&     provider,
                                                                           const std::string& dataset_id);
    std::expected<void, ConsensusError>        submit_mining_proof(MiningState&        state,
                                                                   std::uint64_t       epoch,
                                                                   const ActorId&      provider,
                                                                   const std::string&  dataset_id,
                                                                   const StorageProof& proof);

    // The caller obtains the budget from the committed emission policy and records returned native payouts
    // with this transition. A budget is supplied only on the first section of an epoch.
    std::expected<MiningPayouts, ConsensusError> advance_mining_state(MiningState&  state,
                                                                      std::uint64_t section,
                                                                      std::uint64_t epoch_budget_units,
                                                                      const MiningFinalityReader& read_finality,
                                                                      const LightClientVerifier&  verifier);
    std::string                                  mining_state_root(const MiningState& state);
} // namespace ExtraChain::Consensus
