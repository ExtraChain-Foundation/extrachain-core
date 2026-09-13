#pragma once

#include "chain/transaction.h"
#include "consensus/mining_settlement.h"

namespace ExtraChain::Consensus {
    inline constexpr std::size_t MaximumMiningSettlementBytes = 8 * 1024 * 1024;

    std::string mining_settlement_identity(const ActorId& network, const SectionId& section);
    std::expected<Transaction, ConsensusError> make_mining_settlement_transaction(
        const MiningSettlementRecord& record);
    std::expected<MiningSettlementRecord, ConsensusError> decode_mining_settlement_transaction(
        const Transaction& transaction);
    std::expected<MiningPayouts, ConsensusError> verify_mining_settlement_transaction(
        const Transaction&         transaction,
        const ActorId&             network,
        const LightClientVerifier& verifier);

    // Balance replay uses records from validated history. Admission must authenticate finality first.
    std::expected<std::map<ActorId, BigNumberFloat>, ConsensusError> mining_settlement_deltas(
        const Transaction& transaction);
} // namespace ExtraChain::Consensus
