#pragma once

#include "consensus/mining_epoch.h"
#include "consensus/mining_policy.h"

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
        // The first epoch that has not started. Epochs start in block-time order, frozen or not (a period
        // without budget starts one too); a period no height reached never starts.
        std::uint64_t next_epoch = 0;

        MSGPACK_DEFINE(network,
                       section,
                       reserved_units,
                       minted_units,
                       emission_policy_hash,
                       registrations,
                       epochs,
                       next_epoch)
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
                                                                   const StorageProof& proof);

    // The caller records returned native payouts with this transition. Epochs change on the first section of
    // a height, by the time of its parent block: the height's own time is not set when its state is built.
    std::expected<MiningPayouts, ConsensusError> advance_mining_state(
        MiningState&                               state,
        std::uint64_t                              section,
        std::uint64_t                              parent_time,
        const std::optional<MiningEmissionPolicy>& policy,
        const MiningFinalityReader&                read_finality,
        const LightClientVerifier&                 verifier);
    std::string                                  mining_state_root(const MiningState& state);
} // namespace ExtraChain::Consensus
