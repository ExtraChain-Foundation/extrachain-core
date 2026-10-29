#include "consensus/consensus_service.h"

#include <algorithm>
#include <filesystem>
#include <set>

#include "chain/dag.h"
#include "chain/actor_index.h"
#include "contracts/contract_manager.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "network/wire_format.h"
#include "utils/file_io.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        LightContractSnapshotV1 snapshot_header(const Contracts::ContractSummary& summary) {
            return {
                .contract_id      = summary.contract_id,
                .owner_id         = summary.owner_id,
                .kind             = summary.kind,
                .language         = summary.language,
                .version          = summary.version,
                .revision         = summary.revision,
                .module_hash      = summary.module_hash,
                .state_hash       = summary.state_hash,
                .transaction_hash = summary.transaction_hash,
                .section          = summary.section,
            };
        }
    } // namespace

    std::expected<BalanceSnapshotV2, ConsensusError> ConsensusService::balance_snapshot(
        const LightSnapshotRequestV1& request) const {
        std::lock_guard lock(mutex_);
        if (!consensus_ || node_.dag()->mode() != DagMode::Full)
            return std::unexpected(ConsensusError::NotReady);
        if (request.owners.size() > MaximumLightContracts || request.contracts.size() > MaximumLightContracts
            || request.intents.size() > MaximumLightReceipts)
            return std::unexpected(ConsensusError::DataTooLarge);
        const auto height = consensus_->engine().safety_state().finalized_height;
        if (height == 0)
            return std::unexpected(ConsensusError::NotReady);
        const auto proofs = consensus_->engine().finality_proofs_after(height - 1, 1);
        if (!proofs.has_value() || proofs.value().empty())
            return std::unexpected(ConsensusError::DataUnavailable);
        const auto& proof = proofs.value().front();
        if (proof.finalized_proposal.header.height != height)
            return std::unexpected(ConsensusError::InvalidHeight);
        BalanceSnapshotV2 result {
            .balances =
                node_.dag()->calculate_actors_balance(node_.actor_index()->read_all_actors_ids(),
                                                      SectionId(proof.finalized_proposal.header.dag_section)),
            .proof  = proof,
            .nonces = committed_nonces_,
        };
        std::erase_if(result.nonces, [](const auto& entry) {
            return entry.second == 0;
        });
        if (result.balances.size() > MaximumSnapshotBalances || result.nonces.size() > MaximumSnapshotBalances
            || balance_snapshot_root(result.balances, result.nonces)
                   != proof.finalized_proposal.state.account_state_root)
            return std::unexpected(ConsensusError::InvalidRoot);
        std::map<std::string, Contracts::ContractSummary> catalog;
        Contracts::ContractCatalogFilter                  filter { .limit = 100 };
        do {
            const auto page = node_.dag()->cache().list_contracts(filter);
            for (const auto& summary : page.items) {
                if (summary.section <= proof.finalized_proposal.header.dag_section)
                    catalog.insert_or_assign(summary.contract_id, summary);
            }
            filter.cursor = page.next_cursor;
        } while (filter.cursor.has_value());
        std::vector<std::pair<std::string, std::string>> entries;
        entries.reserve(catalog.size());
        for (const auto& [id, summary] : catalog)
            entries.emplace_back(id, contract_snapshot_value(snapshot_header(summary)));
        if (segmented_state_root("contracts", entries) != proof.finalized_proposal.state.contract_state_root)
            return std::unexpected(ConsensusError::DataUnavailable);
        std::vector<std::string> selected;
        for (const auto& id : request.contracts) {
            if (catalog.contains(id.to_string()) && std::ranges::find(selected, id.to_string()) == selected.end())
                selected.push_back(id.to_string());
        }
        for (const auto& [id, summary] : catalog) {
            const auto owner = ActorId::create(summary.owner_id);
            if (selected.size() >= MaximumLightContracts)
                break;
            if (owner.has_value() && std::ranges::find(request.owners, owner.value()) != request.owners.end()
                && std::ranges::find(selected, id) == selected.end())
                selected.push_back(id);
        }
        for (const auto& id : selected) {
            const auto& summary = catalog.at(id);
            const auto  record  = node_.contract_manager()->inspect(id);
            if (!record.has_value() || record.value().active_version != summary.version
                || record.value().versions.size() < summary.version)
                return std::unexpected(ConsensusError::DataUnavailable);
            const auto& active = record.value().versions.at(summary.version - 1);
            if (active.revisions.empty() || active.revisions.back().transaction_hash != summary.transaction_hash)
                return std::unexpected(ConsensusError::DataUnavailable);
            const auto transaction =
                node_.dag()->find_transaction(SectionId(summary.section), summary.transaction_hash);
            const auto inclusion = make_state_entry_proof("contracts", entries, id);
            if (!transaction.has_value() || !inclusion.has_value())
                return std::unexpected(ConsensusError::DataUnavailable);
            auto snapshot        = snapshot_header(summary);
            snapshot.module      = active.module;
            snapshot.state       = active.revisions.back().state;
            snapshot.transaction = transaction.value();
            snapshot.proof       = inclusion.value();
            if (!verify_contract_snapshot(snapshot, proof.finalized_proposal.state.contract_state_root)
                     .has_value())
                return std::unexpected(ConsensusError::InvalidProof);
            result.contracts.push_back(std::move(snapshot));
        }
        std::set<std::string> included;
        for (const auto& hash : request.intents) {
            if (hash.size() != 64 || !included.insert(hash).second)
                continue;
            const auto receipt = consensus_->engine().transaction_inclusion_proof(hash);
            if (receipt.has_value() && receipt.value().has_value())
                result.receipts.push_back(receipt.value().value());
        }
        if (MessagePack::serialize(result).size() > MaximumBalanceSnapshotBytes)
            return std::unexpected(ConsensusError::DataTooLarge);
        return result;
    }

    std::expected<void, ConsensusError> ConsensusService::activate_light(const ActorId& expected_network) {
        auto verifier = load_light_verifier();
        if (!verifier.has_value())
            return std::unexpected(verifier.error());
        if (!expected_network.is_zero()
            && verifier.value().snapshot().active_validators.network_id != expected_network)
            return std::unexpected(ConsensusError::InvalidNetwork);
        if (node_.network_id().is_zero())
            node_.actor_index()->set_network_id(verifier.value().snapshot().active_validators.network_id);
        light_verifier_ = std::move(verifier.value());
        if (!intent_store_)
            intent_store_ = std::make_unique<IntentStore>(directory_ / "intent-pool.sqlite");
        const auto opened = intent_store_->open();
        if (!opened.has_value())
            return std::unexpected(opened.error());
        if (connections_.empty()) {
            connections_.emplace_back(node_.network()->socket_activated_event().subscribe(
                [this](const std::string&, const std::string& identifier) {
                    peer_connected(identifier);
                }));
        }
        std::error_code error;
        const auto      path = directory_ / "light-state.msgpack";
        if (!std::filesystem::exists(path, error))
            return error ? std::expected<void, ConsensusError>(std::unexpected(ConsensusError::StorageFailure))
                         : std::expected<void, ConsensusError> {};
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > MaximumBalanceSnapshotBytes)
            return std::unexpected(ConsensusError::StorageFailure);
        const auto bytes = FileIo::read_all(path);
        if (!bytes.has_value())
            return std::unexpected(ConsensusError::StorageFailure);
        const auto snapshot = decode_balance_snapshot(bytes.value());
        if (!snapshot.has_value() || !apply_light_snapshot(snapshot.value(), false))
            return std::unexpected(ConsensusError::InvalidProof);
        return {};
    }

    bool ConsensusService::apply_light_snapshot(const BalanceSnapshotV2& snapshot, bool persist) {
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        if (!light_verifier_.has_value() || !intent_store_
            || !verify_balance_snapshot(snapshot, light_verifier_.value()))
            return false;
        auto verifier = light_verifier_.value();
        if (!verifier.advance(snapshot.proof).has_value())
            return false;
        if (persist) {
            const auto bytes = MessagePack::serialize(snapshot);
            if (bytes.size() > MaximumBalanceSnapshotBytes
                || !FileIo::write_atomic(directory_ / "light-state.msgpack", { bytes.data(), bytes.size() })
                        .has_value()
                || !verifier.save(directory_ / "light-client.msgpack").has_value())
                return false;
        }
        const auto stored = intent_store_->apply_verified_light_state(snapshot.nonces, snapshot.receipts);
        if (!stored.has_value())
            return false;
        const bool advanced = !light_snapshot_.has_value()
                              || snapshot.proof.finalized_proposal.header.height
                                     > light_snapshot_.value().proof.finalized_proposal.header.height;
        committed_nonces_         = snapshot.nonces;
        light_verifier_           = std::move(verifier);
        light_snapshot_           = snapshot;
        pending_intents_restored_ = false;
        intent_pool_.discard_committed(committed_nonces_);
        if (advanced && restore_pending_intents().has_value()) {
            for (const auto& envelope :
                 intent_pool_.ready(committed_nonces_, intent_height(), 64, 4 * 1024 * 1024))
                node_.network()->send_message(envelope,
                                              MessageType::ConsensusIntent,
                                              SendMode::Broadcast,
                                              MessageStatus::NoStatus);
        }
        return true;
    }

    bool ConsensusService::accept_balance_snapshot(const BalanceSnapshotV2& snapshot) {
        std::lock_guard lock(mutex_);
        if (node_.dag()->mode() != DagMode::Light
            || snapshot.proof.finalized_proposal.header.network_id != node_.network_id())
            return false;
        if (!light_verifier_.has_value()) {
            if (!activate_light().has_value())
                return false;
        }
        const auto trusted = light_verifier_.value().snapshot();
        if (snapshot.proof.finalized_proposal.header.epoch > trusted.active_validators.epoch
            && trusted.trust_anchor.has_value()) {
            request_light_history();
            return false;
        }
        return apply_light_snapshot(snapshot, true);
    }

    std::optional<std::uint64_t> ConsensusService::verified_light_section() const {
        std::lock_guard lock(mutex_);
        if (!light_snapshot_.has_value())
            return std::nullopt;
        return light_snapshot_.value().proof.finalized_proposal.header.dag_section;
    }

    LightSnapshotRequestV1 ConsensusService::light_snapshot_request() const {
        std::lock_guard        lock(mutex_);
        LightSnapshotRequestV1 request;
        std::set<ActorId>      owners;
        std::set<ActorId>      contracts = watched_light_contracts_;
        if (node_.account_controller()->has_current_profile()) {
            for (const auto& actor : node_.account_controller()->current_profile().actors()) {
                if (actor.type() == ActorType::Service)
                    contracts.insert(actor.id());
                else
                    owners.insert(actor.id());
            }
        }
        for (const auto& owner : owners) {
            if (request.owners.size() == MaximumLightContracts)
                break;
            request.owners.push_back(owner);
        }
        for (const auto& contract : contracts) {
            if (request.contracts.size() == MaximumLightContracts)
                break;
            request.contracts.push_back(contract);
        }
        if (intent_store_) {
            const auto pending = intent_store_->load_pending();
            if (pending.has_value()) {
                for (const auto& envelope : pending.value()) {
                    if (request.intents.size() == MaximumLightReceipts)
                        break;
                    request.intents.push_back(hash_intent(envelope.intent));
                }
            }
        }
        return request;
    }

    bool ConsensusService::watch_light_contract(const ActorId& contract) {
        bool added = false;
        {
            std::lock_guard lock(mutex_);
            if (!watched_light_contracts_.contains(contract)) {
                if (watched_light_contracts_.size() >= MaximumLightContracts)
                    return false;
                added = watched_light_contracts_.insert(contract).second;
            }
        }
        if (added && node_.dag()->mode() == DagMode::Light) {
            Responder peers(nullptr);
            for (const auto& peer : node_.network()->active_full_peer_identifiers())
                peers.add_identifier(peer);
            node_.dag()->request_light(peers);
        }
        return true;
    }

    std::expected<Contracts::ContractRecord, Contracts::ContractFailure> ConsensusService::light_contract(
        std::string_view contract_id) const {
        std::lock_guard lock(mutex_);
        const auto      unavailable = [] {
            return std::unexpected(Contracts::ContractFailure {
                     .error  = Contracts::ContractError::StorageError,
                     .detail = "Verified contract state is not available. Refresh the contract and try again.",
            });
        };
        if (!light_snapshot_.has_value())
            return unavailable();
        const auto& contracts = light_snapshot_.value().contracts;
        const auto  found     = std::ranges::find(contracts, contract_id, &LightContractSnapshotV1::contract_id);
        if (found == contracts.end())
            return unavailable();
        const auto record =
            verify_contract_snapshot(*found,
                                     light_snapshot_.value().proof.finalized_proposal.state.contract_state_root);
        return record.has_value()
                   ? std::expected<Contracts::ContractRecord, Contracts::ContractFailure>(record.value())
                   : std::expected<Contracts::ContractRecord, Contracts::ContractFailure>(unavailable());
    }

    Contracts::ContractCatalogPage ConsensusService::light_contracts(
        const Contracts::ContractCatalogFilter& filter) const {
        std::lock_guard                lock(mutex_);
        Contracts::ContractCatalogPage page;
        if (!light_snapshot_.has_value())
            return page;
        std::vector<Contracts::ContractSummary> summaries;
        for (const auto& contract : light_snapshot_.value().contracts) {
            if ((!filter.owner_id.has_value() || contract.owner_id == filter.owner_id.value())
                && (!filter.kind.has_value() || contract.kind == filter.kind.value())
                && (!filter.cursor.has_value() || contract.contract_id > filter.cursor.value()))
                summaries.push_back(light_contract_summary(contract));
        }
        std::ranges::sort(summaries, {}, &Contracts::ContractSummary::contract_id);
        const auto limit = std::clamp<std::size_t>(filter.limit, 1, MaximumLightContracts);
        if (summaries.size() > limit) {
            page.next_cursor = summaries[limit - 1].contract_id;
            summaries.resize(limit);
        }
        page.items = std::move(summaries);
        return page;
    }
} // namespace ExtraChain::Consensus
