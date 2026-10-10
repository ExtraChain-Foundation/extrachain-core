#pragma once

#include "consensus/storage_proof.h"

namespace ExtraChain::Consensus {
    class LightClientVerifier;
    // Epochs follow block time (MiningEmissionPolicy::epoch_ms), not heights: with fast blocks a
    // per-height epoch froze every registration again each second and left a proof window of two heights.
    inline constexpr std::uint64_t MaximumMiningEpoch         = std::numeric_limits<std::int64_t>::max();
    // A checkpoint's finality proof needs two certified descendants, so it is readable from the first
    // section after them.
    inline constexpr std::uint64_t MiningFinalitySections     = 2 * ShadowSectionInterval;
    // How long a proof request sent into a window that has not closed yet stays valid.
    inline constexpr std::uint64_t MaximumMiningProofHeights  = 32;
    inline constexpr std::size_t   MaximumMiningRegistrations = 8192;
    inline constexpr std::size_t   MaximumMiningDatasets      = 1024;
    inline constexpr std::size_t   MaximumMiningEpochs        = 8;

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
        // Last section of the height that started the next epoch; its header is the challenge.
        // Zero while this epoch is the newest.
        std::uint64_t challenge_section = 0;
        // Parent block time that closes the proof window; zero until the window opens. The height whose
        // parent time reaches it is the window's last one and sets proof_last_section.
        std::uint64_t proof_closes_ms = 0;

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
                                                    state.claimed,
                                                    state.challenge_section,
                                                    state.proof_closes_ms);
        }

    public:
        template <typename Packer>
        void msgpack_pack(Packer& packer) const {
            fields(*this).msgpack_pack(packer);
        }

        void msgpack_unpack(const msgpack::object& object) {
            // MessagePack's default adapter accepts missing or extra fields and can change a signed root.
            if (object.type != msgpack::type::ARRAY || object.via.array.size != 12)
                throw msgpack::type_error();
            fields(*this).msgpack_unpack(object);
        }

        template <typename Object>
        void msgpack_object(Object* object, msgpack::zone& zone) const {
            fields(*this).msgpack_object(object, zone);
        }
    };

    // The section where the window opens and the one that settles, once the epoch has recorded them.
    std::optional<std::uint64_t> mining_window_opening_section(const MiningEpochState& state);
    std::optional<std::uint64_t> mining_settlement_section(const MiningEpochState& state);
    // Whether a proof applied at this section falls inside the window.
    bool mining_window_accepts(const MiningEpochState& state, std::uint64_t section);

    std::expected<std::uint64_t, ConsensusError>    reserve_mining_emission(std::uint64_t reserved_units,
                                                                            std::uint64_t epoch_units);
    // Registrations made at first_section or later join the next epoch.
    std::expected<MiningEpochState, ConsensusError> freeze_mining_epoch(
        const ActorId&                         network,
        std::uint64_t                          epoch,
        std::uint64_t                          budget_units,
        const std::vector<MiningRegistration>& registrations,
        std::uint64_t                          first_section);

    // The consensus caller must authenticate the fixed finalized checkpoint before opening the window.
    std::expected<void, ConsensusError> open_mining_proof_window(MiningEpochState&       state,
                                                                 const StorageChallenge& challenge,
                                                                 std::uint64_t           first_section,
                                                                 std::uint64_t           closes_ms);
    // The datasets a provider holds in an epoch, in the epoch's order: one proof covers all of them.
    std::vector<StorageDataset>         mining_provider_datasets(const MiningEpochState& state,
                                                                 const ActorId&          provider);
    std::expected<void, ConsensusError> accept_mining_proof(MiningEpochState&   state,
                                                            const ActorId&      provider,
                                                            std::uint64_t       section,
                                                            const StorageProof& proof);
    std::expected<void, ConsensusError> settle_mining_epoch(MiningEpochState& state,
                                                            std::uint64_t     finalized_section);
    std::string                         mining_epoch_root(const MiningEpochState& state);
    std::expected<void, ConsensusError> open_mining_proof_window(MiningEpochState&          state,
                                                                 const FinalityProof&       checkpoint,
                                                                 const LightClientVerifier& verifier,
                                                                 std::uint64_t              closes_ms);
    std::expected<void, ConsensusError> settle_mining_epoch(MiningEpochState&          state,
                                                            const FinalityProof&       checkpoint,
                                                            const LightClientVerifier& verifier);
} // namespace ExtraChain::Consensus
