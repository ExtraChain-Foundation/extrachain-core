#include "consensus/balance_snapshot.h"

#include <algorithm>
#include <limits>
#include <set>

#include "contracts/contract_hash.h"
#include "contracts/contract_module.h"
#include "contracts/contract_transaction.h"
#include "network/wire_format.h"
#include "utils/serialization.h"
#include "utils/exc_utils.h"
#include "utils/msgpack_limits.h"

namespace ExtraChain::Consensus {
    namespace {
        std::optional<ContractTransitionData> contract_transition(const LightContractSnapshotV1& snapshot) {
            if (!snapshot.transaction.meta().has_value())
                return std::nullopt;
            const auto metadata = Json::deserialize<ContractTransactionData>(snapshot.transaction.meta().value());
            if (!metadata.has_value() || metadata.value().schema != 4)
                return std::nullopt;
            if (snapshot.transaction.receiver().to_string() == snapshot.contract_id) {
                const auto& m = metadata.value();
                return ContractTransitionData {
                    .contract_id         = snapshot.contract_id,
                    .kind                = m.kind,
                    .language            = m.language,
                    .method              = m.method,
                    .arguments_base64    = m.arguments_base64,
                    .module_hash         = m.module_hash,
                    .previous_state_hash = m.previous_state_hash,
                    .state_hash          = m.state_hash,
                    .effects_hash        = m.effects_hash,
                    .effects_base64      = m.effects_base64,
                    .version             = m.version,
                    .revision            = m.revision,
                    .checkpoint          = m.checkpoint,
                    .checkpoint_revision = m.checkpoint_revision,
                };
            }
            const auto& transitions = metadata.value().transitions;
            const auto  found =
                std::ranges::find(transitions, snapshot.contract_id, &ContractTransitionData::contract_id);
            if (found == transitions.end()
                || std::ranges::count(transitions, snapshot.contract_id, &ContractTransitionData::contract_id)
                       != 1)
                return std::nullopt;
            return *found;
        }
    } // namespace

    std::expected<BalanceSnapshotV2, ConsensusError> decode_balance_snapshot(std::string_view bytes) {
        if (bytes.empty() || bytes.size() > MaximumBalanceSnapshotBytes
            || !MessagePack::has_bounded_structure(bytes, 1'000'000, MaximumSnapshotBalances, 16))
            return std::unexpected(ConsensusError::DataTooLarge);
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        const auto        snapshot = MessagePack::deserialize<BalanceSnapshotV2>(bytes);
        if (!snapshot.has_value())
            return std::unexpected(ConsensusError::InvalidProof);
        return snapshot.value();
    }

    std::string balance_snapshot_root(const std::map<std::pair<ActorId, ActorId>, BigNumberFloat>& balances,
                                      const std::map<ActorId, std::uint64_t>&                      nonces) {
        std::vector<std::pair<std::string, std::string>> entries;
        entries.reserve(balances.size() + nonces.size());
        for (const auto& [key, amount] : balances) {
            if (amount != 0)
                entries.emplace_back(key.first.to_string() + ':' + key.second.to_string(), amount.to_string());
        }
        for (const auto& [actor, nonce] : nonces) {
            if (nonce != 0)
                entries.emplace_back("@nonce:" + actor.to_string(), std::to_string(nonce));
        }
        return segmented_state_root("accounts", entries);
    }

    std::string contract_snapshot_value(const LightContractSnapshotV1& snapshot) {
        return MessagePack::serialize(std::tuple { snapshot.owner_id,
                                                   snapshot.kind,
                                                   snapshot.language,
                                                   snapshot.version,
                                                   snapshot.revision,
                                                   snapshot.module_hash,
                                                   snapshot.state_hash,
                                                   snapshot.transaction_hash,
                                                   snapshot.section });
    }

    std::expected<Contracts::ContractRecord, ConsensusError> verify_contract_snapshot(
        const LightContractSnapshotV1& snapshot,
        std::string_view               root) {
        const auto id    = ActorId::create(snapshot.contract_id);
        const auto owner = ActorId::create(snapshot.owner_id);
        if (!id.has_value() || id.value().is_zero() || !owner.has_value() || owner.value().is_zero()
            || snapshot.kind.empty() || snapshot.kind.size() > 64 || snapshot.version == 0
            || snapshot.version > MaximumLightContractVersions || snapshot.revision < snapshot.version
            || snapshot.section == 0 || snapshot.module.empty() || snapshot.module.size() > 2 * 1024 * 1024
            || snapshot.state.size() > 1024 * 1024
            || Contracts::content_hash(snapshot.module) != snapshot.module_hash
            || Contracts::content_hash(snapshot.state) != snapshot.state_hash
            || !verify_state_entry_proof("contracts",
                                         snapshot.contract_id,
                                         contract_snapshot_value(snapshot),
                                         snapshot.proof,
                                         root))
            return std::unexpected(ConsensusError::InvalidProof);
        const auto language = Contracts::module_language(snapshot.module);
        if (!language.has_value() || language.value() != snapshot.language)
            return std::unexpected(ConsensusError::InvalidProof);
        auto transaction = snapshot.transaction;
        transaction.update_hash();
        if (transaction.hash() != snapshot.transaction_hash || !is_contract_transaction(transaction.type())
            || transaction.section().to_int().value_or(0) != snapshot.section)
            return std::unexpected(ConsensusError::InvalidProof);
        const auto transition = contract_transition(snapshot);
        if (!transition.has_value() || transition.value().kind != snapshot.kind
            || transition.value().language != snapshot.language || transition.value().version != snapshot.version
            || transition.value().revision != snapshot.revision
            || transition.value().module_hash != snapshot.module_hash
            || transition.value().state_hash != snapshot.state_hash || transition.value().checkpoint_revision == 0
            || transition.value().checkpoint_revision > snapshot.revision
            || (transition.value().checkpoint && transition.value().checkpoint_revision != snapshot.revision))
            return std::unexpected(ConsensusError::InvalidProof);
        Contracts::ContractRecord record {
            .contract_id    = snapshot.contract_id,
            .owner_id       = snapshot.owner_id,
            .kind           = snapshot.kind,
            .language       = snapshot.language,
            .active_version = snapshot.version,
        };
        // Only the active version is proven. Earlier slots contain no historical module or state.
        record.versions.resize(snapshot.version);
        for (std::size_t index = 0; index < record.versions.size(); ++index)
            record.versions[index].version = static_cast<std::uint32_t>(index + 1);
        auto& active       = record.versions.back();
        active.module_hash = snapshot.module_hash;
        active.module      = snapshot.module;
        Contracts::StateRevision revision {
            .revision            = snapshot.revision,
            .block               = snapshot.section,
            .previous_hash       = transition.value().previous_state_hash,
            .state_hash          = snapshot.state_hash,
            .transaction_hash    = snapshot.transaction_hash,
            .author_id           = transaction.sender().to_string(),
            .state               = snapshot.state,
            .checkpoint_revision = transition.value().checkpoint_revision,
        };
        if (transition.value().checkpoint) {
            revision.checkpoint_block            = snapshot.section;
            revision.checkpoint_hash             = snapshot.state_hash;
            revision.checkpoint_transaction_hash = snapshot.transaction_hash;
            revision.checkpoint_author_id        = revision.author_id;
        }
        active.revisions.push_back(std::move(revision));
        return record;
    }

    Contracts::ContractSummary light_contract_summary(const LightContractSnapshotV1& snapshot) {
        Contracts::ContractSummary summary {
            .contract_id      = snapshot.contract_id,
            .owner_id         = snapshot.owner_id,
            .kind             = snapshot.kind,
            .language         = snapshot.language,
            .version          = snapshot.version,
            .revision         = snapshot.revision,
            .module_hash      = snapshot.module_hash,
            .state_hash       = snapshot.state_hash,
            .transaction_hash = snapshot.transaction_hash,
            .section          = snapshot.section,
        };
        const auto transition = contract_transition(snapshot);
        if (transition.has_value()) {
            summary.checkpoint_revision = transition.value().checkpoint_revision;
            summary.replay_depth        = snapshot.revision - summary.checkpoint_revision;
            if (transition.value().checkpoint) {
                summary.checkpoint_section          = snapshot.section;
                summary.checkpoint_state_hash       = snapshot.state_hash;
                summary.checkpoint_transaction_hash = snapshot.transaction_hash;
            }
        }
        return summary;
    }

    bool verify_balance_snapshot(const BalanceSnapshotV2& snapshot, const LightClientVerifier& verifier) {
        if (snapshot.balances.size() > MaximumSnapshotBalances || snapshot.nonces.size() > MaximumSnapshotBalances
            || snapshot.contracts.size() > MaximumLightContracts || snapshot.receipts.size() > MaximumLightReceipts
            || std::ranges::any_of(snapshot.balances,
                                   [](const auto& entry) {
                                       return entry.first.first.is_zero() || entry.second < 0;
                                   })
            || std::ranges::any_of(snapshot.nonces, [](const auto& entry) {
                   return entry.first.is_zero() || entry.second == 0
                          || entry.second > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
               }))
            return false;
        const auto& proposal = snapshot.proof.finalized_proposal;
        const auto& header   = proposal.header;
        const auto& state    = proposal.state;
        if (header.protocol_version != ProtocolVersion || state.protocol_version != ProtocolVersion
            || state.network_id != header.network_id || state.epoch != header.epoch
            || state.height != header.height || state.section_root != header.section_root
            || state.validator_set_hash != header.validator_set_hash
            || header.dag_section != proposal.batch.last_section
            || hash_state_commitment(state) != header.state_commitment
            || balance_snapshot_root(snapshot.balances, snapshot.nonces) != state.account_state_root
            || !verifier.verify_finality_proof(snapshot.proof))
            return false;
        std::set<std::string> contracts;
        for (const auto& contract : snapshot.contracts) {
            if (contract.section > header.dag_section || !contracts.insert(contract.contract_id).second
                || !verify_contract_snapshot(contract, state.contract_state_root).has_value())
                return false;
        }
        std::set<std::string> receipts;
        for (const auto& receipt : snapshot.receipts) {
            if (receipt.height > header.height || !receipts.insert(receipt.transaction_hash).second
                || receipt.network_id != header.network_id || !verifier.verify_transaction_proof(receipt))
                return false;
        }
        return true;
    }
} // namespace ExtraChain::Consensus
