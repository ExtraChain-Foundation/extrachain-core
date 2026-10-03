#pragma once

#include "consensus/storage_proof.h"

namespace ExtraChain::Consensus {
    class LightClientVerifier;
    inline constexpr std::uint64_t MiningProofWindowSections  = 2 * ShadowSectionInterval;
    inline constexpr std::size_t   MaximumMiningRegistrations = 8192;
    inline constexpr std::size_t   MaximumMiningDatasets      = 1024;

    struct MiningEpochSchedule {
        std::uint64_t first_section;
        std::uint64_t last_section;
        std::uint64_t challenge_section;
        std::uint64_t proof_first_section;
        std::uint64_t proof_last_section;
        std::uint64_t settlement_first_section;
    };

    std::expected<MiningEpochSchedule, ConsensusError> mining_epoch_schedule(std::uint64_t epoch);

    struct MiningRegistration {
        ActorId        provider;
        StorageDataset dataset;
        std::uint64_t  section = 0;
    };

    struct MiningDatasetBudget {
        StorageDataset        dataset;
        std::uint64_t         units = 0;
        std::set<std::string> providers;
        std::set<std::string> accepted;

        MSGPACK_DEFINE(dataset, units, providers, accepted)
    };

    struct MiningEpochState {
        ActorId                                    network;
        std::uint64_t                              epoch        = 0;
        std::uint64_t                              budget_units = 0;
        std::map<std::string, MiningDatasetBudget> datasets;
        std::optional<StorageChallenge>            challenge;
        std::uint64_t                              proof_first_section = 0;
        std::uint64_t                              proof_last_section  = 0;
        bool                                       settled             = false;
        std::map<std::string, std::uint64_t>       rewards;
        // Historical bytes and proof roots include this field. New epochs never use individual claims.
        std::set<std::string> claimed;

    private:
        static auto fields(auto& state) {
            return msgpack::type::make_define_array(state.network,
                                                    state.epoch,
                                                    state.budget_units,
                                                    state.datasets,
                                                    state.challenge,
                                                    state.proof_first_section,
                                                    state.proof_last_section,
                                                    state.settled,
                                                    state.rewards,
                                                    state.claimed);
        }

    public:
        template <typename Packer>
        void msgpack_pack(Packer& packer) const {
            fields(*this).msgpack_pack(packer);
        }

        void msgpack_unpack(const msgpack::object& object) {
            // MessagePack's default adapter accepts missing or extra fields and can change a signed root.
            if (object.type != msgpack::type::ARRAY || object.via.array.size != 10)
                throw msgpack::type_error();
            fields(*this).msgpack_unpack(object);
        }

        template <typename Object>
        void msgpack_object(Object* object, msgpack::zone& zone) const {
            fields(*this).msgpack_object(object, zone);
        }
    };

    std::expected<std::uint64_t, ConsensusError>    reserve_mining_emission(std::uint64_t reserved_units,
                                                                            std::uint64_t epoch_units);
    std::expected<MiningEpochState, ConsensusError> freeze_mining_epoch(
        const ActorId&                         network,
        std::uint64_t                          epoch,
        std::uint64_t                          budget_units,
        const std::vector<MiningRegistration>& registrations);

    // The consensus caller must authenticate the fixed finalized checkpoint before opening the window.
    std::expected<void, ConsensusError> open_mining_proof_window(MiningEpochState&       state,
                                                                 const StorageChallenge& challenge,
                                                                 std::uint64_t           first_section);
    std::expected<void, ConsensusError> accept_mining_proof(MiningEpochState&   state,
                                                            const ActorId&      provider,
                                                            const std::string&  dataset_id,
                                                            std::uint64_t       section,
                                                            const StorageProof& proof);
    std::expected<void, ConsensusError> settle_mining_epoch(MiningEpochState& state,
                                                            std::uint64_t     finalized_section);
    std::string                         mining_epoch_root(const MiningEpochState& state);
    std::expected<void, ConsensusError> open_mining_proof_window(MiningEpochState&          state,
                                                                 const FinalityProof&       checkpoint,
                                                                 const LightClientVerifier& verifier);
    std::expected<void, ConsensusError> settle_mining_epoch(MiningEpochState&          state,
                                                            const FinalityProof&       checkpoint,
                                                            const LightClientVerifier& verifier);
} // namespace ExtraChain::Consensus
