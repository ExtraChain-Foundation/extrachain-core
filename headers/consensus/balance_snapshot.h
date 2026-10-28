#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>

#include "consensus/light_client.h"
#include "utils/bignumber_float.h"

namespace ExtraChain::Consensus {
    inline constexpr std::size_t MaximumSnapshotBalances     = 100'000;
    inline constexpr std::size_t MaximumBalanceSnapshotBytes = 64 * 1024 * 1024;

    struct BalanceSnapshotV1 {
        std::map<std::pair<ActorId, ActorId>, BigNumberFloat> balances;
        FinalityProof                                         proof;

        MSGPACK_DEFINE(balances, proof)
    };

    EXTRACHAIN_EXPORT std::string balance_snapshot_root(
        const std::map<std::pair<ActorId, ActorId>, BigNumberFloat> &balances);
    EXTRACHAIN_EXPORT bool verify_balance_snapshot(const BalanceSnapshotV1   &snapshot,
                                                   const LightClientVerifier &verifier);
} // namespace ExtraChain::Consensus
