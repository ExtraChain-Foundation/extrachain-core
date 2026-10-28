#include "consensus/balance_snapshot.h"

namespace ExtraChain::Consensus {
    std::string balance_snapshot_root(const std::map<std::pair<ActorId, ActorId>, BigNumberFloat> &balances) {
        std::vector<std::pair<std::string, std::string>> entries;
        entries.reserve(balances.size());
        for (const auto &[key, amount] : balances) {
            if (amount != 0) {
                entries.emplace_back(key.first.to_string() + ':' + key.second.to_string(), amount.to_string());
            }
        }
        return segmented_state_root("accounts", entries);
    }

    bool verify_balance_snapshot(const BalanceSnapshotV1 &snapshot, const LightClientVerifier &verifier) {
        if (snapshot.balances.size() > MaximumSnapshotBalances
            || std::ranges::any_of(snapshot.balances, [](const auto &entry) {
                   return entry.first.first.is_zero() || entry.second < 0;
               })) {
            return false;
        }
        const auto &proposal = snapshot.proof.finalized_proposal;
        const auto &header   = proposal.header;
        const auto &state    = proposal.state;
        return header.protocol_version == ProtocolVersion && state.protocol_version == ProtocolVersion
               && state.network_id == header.network_id && state.epoch == header.epoch
               && state.height == header.height && state.section_root == header.section_root
               && state.validator_set_hash == header.validator_set_hash
               && header.dag_section == proposal.batch.last_section
               && hash_state_commitment(state) == header.state_commitment
               && balance_snapshot_root(snapshot.balances) == state.account_state_root
               && verifier.verify_finality_proof(snapshot.proof);
    }
} // namespace ExtraChain::Consensus
