#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "chain/transaction.h"
#include "consensus/light_client.h"
#include "contracts/contract_types.h"
#include "utils/bignumber_float.h"

namespace ExtraChain::Consensus {
    inline constexpr std::size_t MaximumSnapshotBalances      = 100'000;
    inline constexpr std::size_t MaximumBalanceSnapshotBytes  = 64 * 1024 * 1024;
    inline constexpr std::size_t MaximumLightContracts        = 32;
    inline constexpr std::size_t MaximumLightContractVersions = 4096;
    inline constexpr std::size_t MaximumLightReceipts         = 64;

    struct LightSnapshotRequestV1 {
        std::vector<ActorId>     owners;
        std::vector<ActorId>     contracts;
        std::vector<std::string> intents;

        MSGPACK_DEFINE(owners, contracts, intents)
    };

    struct LightContractSnapshotV1 {
        std::string               contract_id;
        std::string               owner_id;
        std::string               kind;
        std::string               language;
        std::uint32_t             version  = 1;
        std::uint64_t             revision = 0;
        std::string               module_hash;
        std::string               state_hash;
        std::string               transaction_hash;
        std::uint64_t             section = 0;
        std::vector<std::uint8_t> module;
        std::vector<std::uint8_t> state;
        Transaction               transaction;
        StateEntryProofV1         proof;

        MSGPACK_DEFINE(contract_id,
                       owner_id,
                       kind,
                       language,
                       version,
                       revision,
                       module_hash,
                       state_hash,
                       transaction_hash,
                       section,
                       module,
                       state,
                       transaction,
                       proof)
    };

    struct BalanceSnapshotV2 {
        std::map<std::pair<ActorId, ActorId>, BigNumberFloat> balances;
        FinalityProof                                         proof;
        std::map<ActorId, std::uint64_t>                      nonces;
        std::vector<LightContractSnapshotV1>                  contracts;
        std::vector<TransactionInclusionProofV1>              receipts;

        MSGPACK_DEFINE(balances, proof, nonces, contracts, receipts)
    };

    EXTRACHAIN_EXPORT std::string balance_snapshot_root(
        const std::map<std::pair<ActorId, ActorId>, BigNumberFloat>& balances,
        const std::map<ActorId, std::uint64_t>&                      nonces = {});
    EXTRACHAIN_EXPORT std::string contract_snapshot_value(const LightContractSnapshotV1& snapshot);
    EXTRACHAIN_EXPORT std::expected<Contracts::ContractRecord, ConsensusError> verify_contract_snapshot(
        const LightContractSnapshotV1& snapshot,
        std::string_view               root);
    EXTRACHAIN_EXPORT Contracts::ContractSummary light_contract_summary(const LightContractSnapshotV1& snapshot);
    EXTRACHAIN_EXPORT bool                       verify_balance_snapshot(const BalanceSnapshotV2&   snapshot,
                                                                         const LightClientVerifier& verifier);
} // namespace ExtraChain::Consensus
