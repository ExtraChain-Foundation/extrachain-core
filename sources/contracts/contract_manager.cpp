/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "contracts/contract_manager.h"

#include <algorithm>
#include <mutex>

#include <QByteArrayView>
#include <QCryptographicHash>

#include "contracts/contract_codec.h"

namespace ExtraChain::Contracts {
    namespace {

        ContractFailure failure(ContractError error, std::string detail) {
            return { error, std::move(detail) };
        }

        std::string hash(std::span<const std::uint8_t> value) {
            QCryptographicHash hasher(QCryptographicHash::Blake2b_256);
            hasher.addData(QByteArrayView(reinterpret_cast<const char *>(value.data()),
                                          static_cast<qsizetype>(value.size())));
            return hasher.result().toHex().toStdString();
        }

        const StateRevision &latest_revision(const ContractVersion &version) {
            return version.revisions.back();
        }

        ContractReceipt receipt(const ContractRecord &record, const ContractOutput &output) {
            const auto &version  = record.versions[record.active_version - 1];
            const auto &revision = latest_revision(version);
            return {
                .contract_id         = record.contract_id,
                .version             = version.version,
                .revision            = revision.revision,
                .previous_state_hash = revision.previous_hash,
                .state_hash          = revision.state_hash,
                .data                = output.data,
                .events              = output.events,
            };
        }

    } // namespace

    void retain_current_contract_state(ContractRecord &record) {
        for (auto &version : record.versions) {
            if (version.version == record.active_version) {
                if (version.revisions.size() > 1) {
                    auto latest = std::move(version.revisions.back());
                    version.revisions.clear();
                    version.revisions.push_back(std::move(latest));
                }
            } else {
                version.module.clear();
                version.revisions.clear();
            }
        }
    }

    std::expected<ContractRecord, ContractFailure> MemoryContractStorage::load(
        std::string_view contract_id) const {
        std::shared_lock lock(mutex_);
        auto             iterator = records_.find(std::string(contract_id));
        if (iterator == records_.end()) {
            return std::unexpected(failure(ContractError::NotFound, "Contract does not exist"));
        }
        return iterator->second;
    }

    std::expected<void, ContractFailure> MemoryContractStorage::create(const ContractRecord &record) {
        std::unique_lock lock(mutex_);
        auto             current = record;
        retain_current_contract_state(current);
        if (!records_.emplace(current.contract_id, std::move(current)).second) {
            return std::unexpected(failure(ContractError::AlreadyExists, "Contract already exists"));
        }
        return {};
    }

    std::expected<void, ContractFailure> MemoryContractStorage::stage(const ContractRecord &) {
        return {};
    }

    std::expected<void, ContractFailure> MemoryContractStorage::replace(const ContractRecord &record,
                                                                        std::uint32_t         expected_version,
                                                                        std::string_view expected_state_hash) {
        std::unique_lock lock(mutex_);
        auto             iterator = records_.find(record.contract_id);
        if (iterator == records_.end()) {
            return std::unexpected(failure(ContractError::NotFound, "Contract does not exist"));
        }
        const auto &current = iterator->second;
        const auto &version = current.versions[current.active_version - 1];
        if (current.active_version != expected_version || version.revisions.empty()
            || latest_revision(version).state_hash != expected_state_hash) {
            return std::unexpected(failure(ContractError::Conflict, "Contract state changed during execution"));
        }
        auto current_record = record;
        retain_current_contract_state(current_record);
        iterator->second = std::move(current_record);
        return {};
    }

    ContractManager::ContractManager(std::unique_ptr<ContractStorage> storage, ExecutionLimits limits)
        : storage_(std::move(storage))
        , runtime_(limits)
        , limits_(limits) {
    }

    std::expected<ContractOutput, ContractFailure> ContractManager::evaluate(
        std::span<const std::uint8_t> module,
        std::string_view              sender,
        std::string_view              method,
        std::span<const std::uint8_t> arguments,
        std::span<const std::uint8_t> state,
        std::uint64_t                 block) const {
        auto input     = Codec::encode_request(sender, method, arguments, state, block);
        auto execution = runtime_.invoke(module, input);
        if (!execution.has_value()) {
            return std::unexpected(failure(ContractError::ExecutionFailed, execution.error().detail));
        }
        auto output = Codec::decode_response(execution->output);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        if (output->events.size() > 64) {
            return std::unexpected(failure(ContractError::TooManyEvents, "Contract emitted too many events"));
        }
        if (output->state.size() > 1024 * 1024) {
            return std::unexpected(failure(ContractError::StateTooLarge, "Contract state exceeds the limit"));
        }
        if (!output->ok) {
            return std::unexpected(
                failure(ContractError::ExecutionFailed, output->error.value_or("Contract rejected the call")));
        }
        return output;
    }

    std::expected<StateRevision, ContractFailure> ContractManager::revision(const ContractOutput &output,
                                                                            const StateRevision  *previous,
                                                                            std::uint64_t         block,
                                                                            std::string author_id) const {
        if (previous != nullptr && output.state == previous->state) {
            return std::unexpected(
                failure(ContractError::InvalidResponse, "A state-changing call did not change contract state"));
        }
        const auto next_revision = previous == nullptr ? 1 : previous->revision + 1;
        const auto make_checkpoint =
            previous == nullptr || previous->checkpoint_revision == 0
            || next_revision - previous->checkpoint_revision >= ContractCheckpointInterval;
        return StateRevision {
            .revision                    = next_revision,
            .block                       = block,
            .previous_hash               = previous == nullptr ? std::string() : previous->state_hash,
            .state_hash                  = hash(output.state),
            .transaction_hash            = {},
            .author_id                   = author_id,
            .state                       = output.state,
            .checkpoint_revision         = make_checkpoint ? next_revision : previous->checkpoint_revision,
            .checkpoint_block            = make_checkpoint ? block : previous->checkpoint_block,
            .checkpoint_hash             = make_checkpoint ? hash(output.state) : previous->checkpoint_hash,
            .checkpoint_transaction_hash = make_checkpoint ? std::string() : previous->checkpoint_transaction_hash,
            .checkpoint_storage_id       = make_checkpoint ? std::string() : previous->checkpoint_storage_id,
            .checkpoint_author_id = make_checkpoint ? std::string(author_id) : previous->checkpoint_author_id,
        };
    }

    ContractVersion &ContractManager::active_version(ContractRecord &record) {
        return record.versions.at(record.active_version - 1);
    }

    const ContractVersion &ContractManager::active_version(const ContractRecord &record) {
        return record.versions.at(record.active_version - 1);
    }

    std::expected<ContractReceipt, ContractFailure> ContractManager::deploy(
        std::string                   contract_id,
        std::string                   owner_id,
        std::string                   kind,
        std::span<const std::uint8_t> module,
        std::span<const std::uint8_t> init_arguments,
        std::uint64_t                 block) {
        auto change = prepare_deploy(std::move(contract_id),
                                     std::move(owner_id),
                                     std::move(kind),
                                     module,
                                     init_arguments,
                                     block);
        if (!change.has_value()) {
            return std::unexpected(change.error());
        }
        auto staged = stage(*change);
        if (!staged.has_value()) {
            return std::unexpected(staged.error());
        }
        return commit(std::move(*change));
    }

    std::expected<PreparedContractChange, ContractFailure> ContractManager::prepare_deploy(
        std::string                   contract_id,
        std::string                   owner_id,
        std::string                   kind,
        std::span<const std::uint8_t> module,
        std::span<const std::uint8_t> init_arguments,
        std::uint64_t                 block) {
        std::unique_lock lock(mutex_);
        if (contract_id.empty() || owner_id.empty() || kind.empty() || kind.size() > 64
            || init_arguments.size() > 512 * 1024) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Invalid contract identity"));
        }
        auto output = evaluate(module, owner_id, "init", init_arguments, {}, block);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        auto initial_revision = revision(*output, nullptr, block, owner_id);
        if (!initial_revision.has_value()) {
            return std::unexpected(initial_revision.error());
        }

        ContractRecord record {
            .contract_id    = std::move(contract_id),
            .owner_id       = std::move(owner_id),
            .kind           = std::move(kind),
            .active_version = 1,
            .versions       = { ContractVersion {
                      .version              = 1,
                      .module_hash          = hash(module),
                      .previous_module_hash = {},
                      .module               = { module.begin(), module.end() },
                      .revisions            = { std::move(*initial_revision) },
            } },
        };
        return PreparedContractChange {
            .kind       = ContractChangeKind::Create,
            .record     = std::move(record),
            .output     = std::move(*output),
            .checkpoint = true,
        };
    }

    std::expected<ContractReceipt, ContractFailure> ContractManager::call(std::string_view contract_id,
                                                                          std::string_view sender_id,
                                                                          std::string_view method,
                                                                          std::span<const std::uint8_t> arguments,
                                                                          std::uint64_t                 block) {
        auto change = prepare_call(contract_id, sender_id, method, arguments, block);
        if (!change.has_value()) {
            return std::unexpected(change.error());
        }
        auto staged = stage(*change);
        if (!staged.has_value()) {
            return std::unexpected(staged.error());
        }
        return commit(std::move(*change));
    }

    std::expected<PreparedContractChange, ContractFailure> ContractManager::prepare_call(
        std::string_view              contract_id,
        std::string_view              sender_id,
        std::string_view              method,
        std::span<const std::uint8_t> arguments,
        std::uint64_t                 block) {
        std::unique_lock lock(mutex_);
        if (sender_id.empty() || method.empty() || method.size() > 64 || arguments.size() > 512 * 1024
            || method == "init" || method == "migrate" || method == "authorize_upgrade") {
            return std::unexpected(failure(ContractError::InvalidArguments, "Invalid contract call"));
        }
        auto loaded = storage_->load(contract_id);
        if (!loaded.has_value()) {
            return std::unexpected(loaded.error());
        }
        auto       record   = std::move(*loaded);
        auto      &version  = active_version(record);
        const auto previous = latest_revision(version);
        auto       output   = evaluate(version.module, sender_id, method, arguments, previous.state, block);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        if (output->state == previous.state) {
            auto expected_version = version.version;
            return PreparedContractChange {
                .kind                = ContractChangeKind::ReadOnly,
                .record              = std::move(record),
                .output              = std::move(*output),
                .expected_version    = expected_version,
                .expected_state_hash = previous.state_hash,
            };
        }
        auto next = revision(*output, &previous, block, std::string(sender_id));
        if (!next.has_value()) {
            return std::unexpected(next.error());
        }
        version.revisions.push_back(std::move(*next));
        auto expected_version = version.version;
        return PreparedContractChange {
            .kind                = ContractChangeKind::Replace,
            .record              = std::move(record),
            .output              = std::move(*output),
            .expected_version    = expected_version,
            .expected_state_hash = previous.state_hash,
            .checkpoint = version.revisions.back().checkpoint_revision == version.revisions.back().revision,
        };
    }

    std::expected<ContractReceipt, ContractFailure> ContractManager::query(std::string_view contract_id,
                                                                           std::string_view sender_id,
                                                                           std::string_view method,
                                                                           std::span<const std::uint8_t> arguments,
                                                                           std::uint64_t block) const {
        std::shared_lock lock(mutex_);
        if (sender_id.empty() || method.empty() || method.size() > 64 || arguments.size() > 512 * 1024
            || method == "init" || method == "migrate" || method == "authorize_upgrade") {
            return std::unexpected(failure(ContractError::InvalidArguments, "Invalid contract query"));
        }
        auto record = storage_->load(contract_id);
        if (!record.has_value()) {
            return std::unexpected(record.error());
        }
        const auto &version  = active_version(*record);
        const auto &previous = latest_revision(version);
        auto        output   = evaluate(version.module, sender_id, method, arguments, previous.state, block);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        if (output->state != previous.state) {
            return std::unexpected(failure(ContractError::InvalidArguments, "The method changes contract state"));
        }
        return receipt(*record, *output);
    }

    std::expected<ContractReceipt, ContractFailure> ContractManager::upgrade(
        std::string_view              contract_id,
        std::string_view              sender_id,
        std::span<const std::uint8_t> module,
        std::span<const std::uint8_t> migration_arguments,
        std::uint64_t                 block) {
        auto change = prepare_upgrade(contract_id, sender_id, module, migration_arguments, block);
        if (!change.has_value()) {
            return std::unexpected(change.error());
        }
        auto staged = stage(*change);
        if (!staged.has_value()) {
            return std::unexpected(staged.error());
        }
        return commit(std::move(*change));
    }

    std::expected<PreparedContractChange, ContractFailure> ContractManager::prepare_upgrade(
        std::string_view              contract_id,
        std::string_view              sender_id,
        std::span<const std::uint8_t> module,
        std::span<const std::uint8_t> migration_arguments,
        std::uint64_t                 block) {
        std::unique_lock lock(mutex_);
        if (contract_id.empty() || sender_id.empty() || module.empty() || module.size() > limits_.module_bytes
            || migration_arguments.size() > 512 * 1024) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Invalid contract upgrade"));
        }
        auto loaded = storage_->load(contract_id);
        if (!loaded.has_value()) {
            return std::unexpected(loaded.error());
        }
        auto record = std::move(*loaded);
        if (record.owner_id != sender_id) {
            return std::unexpected(failure(ContractError::InvalidOwner, "Only the contract owner can upgrade"));
        }
        auto      &current                 = active_version(record);
        const auto previous                = latest_revision(current);
        const auto new_module_hash         = hash(module);
        auto       authorization_arguments = Codec::encode_string(new_module_hash);
        auto       authorization           = evaluate(current.module,
                                      sender_id,
                                      "authorize_upgrade",
                                      authorization_arguments,
                                      previous.state,
                                      block);
        if (!authorization.has_value()) {
            return std::unexpected(failure(ContractError::UpgradeDenied, authorization.error().detail));
        }
        auto migration = evaluate(module, sender_id, "migrate", migration_arguments, previous.state, block);
        if (!migration.has_value()) {
            return std::unexpected(migration.error());
        }

        StateRevision migrated {
            .revision             = previous.revision + 1,
            .block                = block,
            .previous_hash        = previous.state_hash,
            .state_hash           = hash(migration->state),
            .transaction_hash     = {},
            .author_id            = std::string(sender_id),
            .state                = migration->state,
            .checkpoint_revision  = previous.revision + 1,
            .checkpoint_block     = block,
            .checkpoint_hash      = hash(migration->state),
            .checkpoint_author_id = std::string(sender_id),
        };
        record.active_version += 1;
        record.versions.push_back(ContractVersion {
            .version              = record.active_version,
            .module_hash          = new_module_hash,
            .previous_module_hash = current.module_hash,
            .module               = { module.begin(), module.end() },
            .revisions            = { std::move(migrated) },
        });
        auto expected_version = record.active_version - 1;
        return PreparedContractChange {
            .kind                = ContractChangeKind::Replace,
            .record              = std::move(record),
            .output              = std::move(*migration),
            .expected_version    = expected_version,
            .expected_state_hash = previous.state_hash,
            .checkpoint          = true,
        };
    }

    std::expected<void, ContractFailure> ContractManager::stage(const PreparedContractChange &change) {
        std::unique_lock lock(mutex_);
        return storage_->stage(change.record);
    }

    std::expected<ContractReceipt, ContractFailure> ContractManager::commit(PreparedContractChange change,
                                                                            std::string transaction_hash) {
        std::unique_lock lock(mutex_);
        auto            &version = active_version(change.record);
        if (!version.revisions.empty() && change.kind != ContractChangeKind::ReadOnly) {
            version.revisions.back().transaction_hash = std::move(transaction_hash);
            if (change.checkpoint) {
                version.revisions.back().checkpoint_transaction_hash = version.revisions.back().transaction_hash;
            }
        }

        std::expected<void, ContractFailure> saved;
        switch (change.kind) {
        case ContractChangeKind::Create:
            saved = storage_->create(change.record);
            break;
        case ContractChangeKind::Replace:
            saved = storage_->replace(change.record, change.expected_version, change.expected_state_hash);
            break;
        case ContractChangeKind::ReadOnly:
            return receipt(change.record, change.output);
        }
        if (!saved.has_value()) {
            return std::unexpected(saved.error());
        }
        return receipt(change.record, change.output);
    }

    std::expected<ContractRecord, ContractFailure> ContractManager::inspect(std::string_view contract_id) const {
        std::shared_lock lock(mutex_);
        return storage_->load(contract_id);
    }

} // namespace ExtraChain::Contracts
