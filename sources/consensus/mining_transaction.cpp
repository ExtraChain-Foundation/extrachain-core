#include "consensus/mining_transaction.h"

#include "utils/exc_utils.h"
#include "utils/serialization.h"
#include "utils/msgpack_limits.h"

namespace ExtraChain::Consensus {
    std::string mining_settlement_identity(const ActorId& network, const SectionId& section) {
        const auto value = section.to_int();
        if (network.is_zero() || !value.has_value() || value.value() < 0)
            return { };
        return Utils::calculate_hash("EXC_NATIVE_MINING_SETTLEMENT_V1:" + network.to_string() + ":"
                                     + std::to_string(value.value()));
    }

    std::expected<Transaction, ConsensusError> make_mining_settlement_transaction(
        const MiningSettlementRecord& record) {
        const auto schedule = mining_epoch_schedule(record.witness.epoch.epoch);
        if (!schedule.has_value() || record.witness.epoch.network.is_zero())
            return std::unexpected(ConsensusError::InvalidIntent);
        const auto bytes = MessagePack::serialize(record);
        if (bytes.size() > MaximumMiningSettlementBytes)
            return std::unexpected(ConsensusError::DataTooLarge);
        Transaction transaction;
        transaction.set_type(TransactionType::MiningSettlement);
        transaction.set_receiver(record.witness.epoch.network);
        transaction.set_section(SectionId(schedule.value().settlement_first_section));
        transaction.set_meta(Utils::to_base64(bytes));
        transaction.update_hash();
        return transaction;
    }

    std::expected<MiningSettlementRecord, ConsensusError> decode_mining_settlement_transaction(
        const Transaction& transaction) {
        if (transaction.type() != TransactionType::MiningSettlement || !transaction.sender().is_zero()
            || transaction.receiver().is_zero() || !transaction.token().is_zero() || transaction.amount() != 0
            || transaction.timestamp() != 0 || !transaction.prev_hashs().empty()
            || transaction.consensus_intent().has_value() || !Utils::is_container_empty(transaction.signature())
            || !transaction.meta().has_value()
            || transaction.meta().value().size() > 4 * ((MaximumMiningSettlementBytes + 2) / 3)
            || transaction.hash() != mining_settlement_identity(transaction.receiver(), transaction.section()))
            return std::unexpected(ConsensusError::InvalidIntent);
        const auto bytes = Utils::from_base64(transaction.meta().value());
        if (!bytes.has_value() || bytes.value().size() > MaximumMiningSettlementBytes
            || !MessagePack::has_bounded_structure(bytes.value(), 262144, MaximumMiningRegistrations, 32))
            return std::unexpected(ConsensusError::DataTooLarge);
        const auto record = MessagePack::deserialize<MiningSettlementRecord>(bytes.value());
        if (!record.has_value() || MessagePack::serialize(record.value()) != bytes.value()
            || record.value().witness.epoch.network != transaction.receiver())
            return std::unexpected(ConsensusError::InvalidProof);
        const auto schedule = mining_epoch_schedule(record.value().witness.epoch.epoch);
        if (!schedule.has_value() || transaction.section() != SectionId(schedule.value().settlement_first_section))
            return std::unexpected(ConsensusError::InvalidHeight);
        return record.value();
    }

    std::expected<MiningPayouts, ConsensusError> verify_mining_settlement_transaction(
        const Transaction&         transaction,
        const ActorId&             network,
        const LightClientVerifier& verifier) {
        if (transaction.receiver() != network)
            return std::unexpected(ConsensusError::InvalidNetwork);
        const auto record = decode_mining_settlement_transaction(transaction);
        if (!record.has_value())
            return std::unexpected(record.error());
        return verify_mining_settlement(record.value(),
                                        mining_epoch_schedule(record.value().witness.epoch.epoch)
                                            .value()
                                            .settlement_first_section,
                                        verifier);
    }

    std::expected<std::map<ActorId, BigNumberFloat>, ConsensusError> mining_settlement_deltas(
        const Transaction& transaction) {
        const auto record = decode_mining_settlement_transaction(transaction);
        if (!record.has_value())
            return std::unexpected(record.error());
        const auto& proposal = record.value().closure.finalized_proposal;
        if (!verify_mining_epoch_witness(record.value().witness, proposal.state.mining_state_root)
            || hash_state_commitment(proposal.state) != proposal.header.state_commitment)
            return std::unexpected(ConsensusError::InvalidProof);
        auto       epoch   = record.value().witness.epoch;
        const auto settled = settle_mining_epoch(epoch, proposal.header.dag_section);
        if (!settled.has_value())
            return std::unexpected(settled.error());
        std::map<ActorId, BigNumberFloat> deltas;
        for (const auto& [provider, units] : epoch.rewards) {
            const auto actor = ActorId::create(provider);
            const auto amount =
                BigNumberFloat::create(fmt::format("{}.{:08}", units / NativeCoinUnits, units % NativeCoinUnits));
            if (!actor.has_value() || actor.value().is_zero() || !amount.has_value())
                return std::unexpected(ConsensusError::InvalidIntent);
            deltas.emplace(actor.value(), amount.value());
        }
        return deltas;
    }
} // namespace ExtraChain::Consensus
