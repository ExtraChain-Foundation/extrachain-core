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

#include <msgpack.hpp>

#include "contracts/contract_codec.h"
#include "contracts/contract_hash.h"
#include "contracts/contract_module.h"
#include "contracts/standard_token.h"

namespace ExtraChain::Contracts {
    namespace {

        ContractFailure failure(ContractError error, std::string detail) {
            return { error, std::move(detail) };
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

        bool valid_amount(std::string_view value) {
            constexpr std::string_view MaximumU128 = "340282366920938463463374607431768211455";
            return !value.empty() && value != "0"
                   && std::ranges::all_of(value,
                                          [](char digit) {
                                              return digit >= '0' && digit <= '9';
                                          })
                   && (value.size() < MaximumU128.size()
                       || (value.size() == MaximumU128.size() && value <= MaximumU128));
        }

        bool valid_token_delta(const ContractEffect &effect) {
            try {
                std::size_t offset = 0;
                auto        handle = msgpack::unpack(reinterpret_cast<const char *>(effect.arguments.data()),
                                              effect.arguments.size(),
                                              offset);
                if (offset != effect.arguments.size()) {
                    return false;
                }
                std::vector<std::tuple<std::string, std::string>> entries;
                handle.get().convert(entries);
                const auto expected_size = effect.operation == "transfer" ? 2U : 1U;
                if ((effect.operation != "mint" && effect.operation != "burn" && effect.operation != "transfer"
                     && effect.operation != "lock")
                    || entries.size() != expected_size) {
                    return false;
                }
                const auto entries_valid = std::ranges::all_of(entries, [](const auto &entry) {
                    return !std::get<0>(entry).empty() && valid_amount(std::get<1>(entry));
                });
                const auto transfer_valid =
                    effect.operation != "transfer" || std::get<1>(entries.front()) == std::get<1>(entries.back());
                return entries_valid && transfer_valid;
            } catch (const std::exception &) {
                return false;
            }
        }

        bool valid_nft_delta(const ContractEffect &effect) {
            try {
                std::size_t offset = 0;
                auto        handle = msgpack::unpack(reinterpret_cast<const char *>(effect.arguments.data()),
                                              effect.arguments.size(),
                                              offset);
                if (offset != effect.arguments.size()) {
                    return false;
                }
                std::vector<std::string> values;
                handle.get().convert(values);
                const auto expected_size = effect.operation == "nft_transfer" ? 3U : 2U;
                if ((effect.operation != "nft_mint" && effect.operation != "nft_transfer"
                     && effect.operation != "nft_burn")
                    || values.size() != expected_size || values.front().empty() || values.front().size() > 39
                    || !std::ranges::all_of(values.front(),
                                            [](char digit) {
                                                return digit >= '0' && digit <= '9';
                                            })
                    || (values.front().size() > 1 && values.front().front() == '0')
                    || (values.front().size() == 39
                        && values.front() > "340282366920938463463374607431768211455")) {
                    return false;
                }
                return std::all_of(values.begin() + 1, values.end(), [](const std::string &actor) {
                    return !actor.empty();
                });
            } catch (const std::exception &) {
                return false;
            }
        }

        bool valid_hash(std::string_view value, bool allow_empty = false) {
            return (allow_empty && value.empty())
                   || (value.size() == 64 && std::ranges::all_of(value, [](char character) {
                           return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
                       }));
        }

        bool valid_logical_key(std::string_view value) {
            if (value.empty() || value.size() > 128 || value.front() == '/' || value.back() == '/') {
                return false;
            }
            if (value == ".." || value.starts_with("../") || value.ends_with("/..")
                || value.find("/../") != std::string_view::npos) {
                return false;
            }
            return std::ranges::all_of(value, [](char character) {
                return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
                       || (character >= '0' && character <= '9') || character == '-' || character == '_'
                       || character == '.' || character == '/';
            });
        }

        bool valid_dfs_write(const ContractEffect &effect,
                             std::string_view      contract_id,
                             std::string_view      sender_id,
                             const VerifiedInputs &verified) {
            if ((effect.target != contract_id && effect.target != sender_id)
                || (effect.operation != "bind" && effect.operation != "tombstone")) {
                return false;
            }
            try {
                std::size_t offset = 0;
                auto        handle = msgpack::unpack(reinterpret_cast<const char *>(effect.arguments.data()),
                                              effect.arguments.size(),
                                              offset);
                if (offset != effect.arguments.size()) {
                    return false;
                }
                std::tuple<std::string, std::string, std::string, std::string> binding;
                handle.get().convert(binding);
                const auto &[logical_key, file_id, content_hash, previous_content_hash] = binding;
                if (!valid_logical_key(logical_key) || !valid_hash(previous_content_hash, true)) {
                    return false;
                }
                if (effect.operation == "tombstone") {
                    return file_id.empty() && content_hash.empty() && !previous_content_hash.empty();
                }
                if (file_id.empty() || !valid_hash(content_hash)) {
                    return false;
                }
                return std::ranges::any_of(verified.dfs, [&](const DfsProof &proof) {
                    return proof.owner_id == effect.target && proof.file_id == file_id
                           && proof.content_hash == content_hash;
                });
            } catch (const std::exception &) {
                return false;
            }
        }

        bool effects_match_contract(const ContractOutput &output,
                                    std::string_view      contract_id,
                                    std::string_view      kind,
                                    std::string_view      sender_id,
                                    const VerifiedInputs &verified = {}) {
            return std::ranges::all_of(output.effects, [&](const ContractEffect &effect) {
                if (effect.kind == ContractEffectKind::ContractCall) {
                    return true;
                }
                if (effect.kind == ContractEffectKind::TokenDelta) {
                    return effect.target == contract_id
                           && ((kind == "fungible-token" && valid_token_delta(effect))
                               || (kind == "non-fungible-token" && valid_nft_delta(effect)));
                }
                return effect.kind == ContractEffectKind::DfsWrite
                       && valid_dfs_write(effect, contract_id, sender_id, verified);
            });
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

    ContractManager::ContractManager(std::unique_ptr<ContractStorage> storage,
                                     ExecutionLimits                  limits,
                                     RuntimeTuning                    tuning)
        : storage_(std::move(storage))
        , runtime_(limits, tuning)
        , limits_(limits) {
    }

    std::expected<ContractOutput, ContractFailure> ContractManager::evaluate(
        std::span<const std::uint8_t> module,
        std::string_view              sender,
        std::string_view              method,
        std::span<const std::uint8_t> arguments,
        std::span<const std::uint8_t> state,
        std::uint64_t                 block,
        std::string_view              contract_id,
        std::string_view              caller,
        const VerifiedInputs         &verified,
        std::uint32_t                 depth) const {
        if (depth > ContractMaximumCallDepth) {
            return std::unexpected(failure(ContractError::CallDepthExceeded, "Contract call depth is too large"));
        }
        if (verified.dag.size() + verified.dfs.size() > ContractMaximumProofs) {
            return std::unexpected(failure(ContractError::TooManyProofs, "Contract input has too many proofs"));
        }
        const ExecutionContext context {
            .sender      = std::string(sender),
            .caller      = caller.empty() ? std::string(sender) : std::string(caller),
            .contract_id = std::string(contract_id),
            .block       = block,
            .depth       = depth,
        };
        auto input     = Codec::encode_request(context, method, arguments, state, verified);
        auto execution = runtime_.invoke(module, input);
        if (!execution.has_value()) {
            return std::unexpected(failure(ContractError::ExecutionFailed, execution.error().detail));
        }
        auto output = Codec::decode_response(execution->output);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        if (output->events.size() > ContractMaximumEvents) {
            return std::unexpected(failure(ContractError::TooManyEvents, "Contract emitted too many events"));
        }
        if (output->effects.size() > ContractMaximumEffects) {
            return std::unexpected(failure(ContractError::TooManyEffects, "Contract emitted too many effects"));
        }
        for (const auto &effect : output->effects) {
            if (effect.operation.empty() || effect.operation.size() > 64 || effect.target.size() > 128
                || effect.arguments.size() > 512 * 1024) {
                return std::unexpected(
                    failure(ContractError::InvalidResponse, "Contract emitted an invalid effect"));
            }
            if (effect.target.empty()) {
                return std::unexpected(
                    failure(ContractError::InvalidResponse, "Contract call effect has no target"));
            }
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
        if (previous != nullptr && output.state == previous->state && output.effects.empty()) {
            return std::unexpected(
                failure(ContractError::InvalidResponse, "A state-changing call did not change contract state"));
        }
        const auto next_revision = previous == nullptr ? 1 : previous->revision + 1;
        const auto make_checkpoint =
            previous == nullptr || previous->checkpoint_revision == 0
            || next_revision - previous->checkpoint_revision >= ContractCheckpointInterval;
        return StateRevision {
            .revision            = next_revision,
            .block               = block,
            .previous_hash       = previous == nullptr ? std::string() : previous->state_hash,
            .state_hash          = content_hash(output.state),
            .transaction_hash    = {},
            .author_id           = author_id,
            .state               = output.state,
            .checkpoint_revision = make_checkpoint ? next_revision : previous->checkpoint_revision,
            .checkpoint_block    = make_checkpoint ? block : previous->checkpoint_block,
            .checkpoint_hash     = make_checkpoint ? content_hash(output.state) : previous->checkpoint_hash,
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
        auto language = module_language(module);
        if (is_system_token_kind(kind) && !language.has_value()) {
            return std::unexpected(failure(ContractError::InvalidModule, language.error()));
        }
        auto output = evaluate(module, owner_id, "init", init_arguments, {}, block, contract_id);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        if (std::ranges::any_of(output.value().effects,
                                [](const ContractEffect &effect) {
                                    return effect.kind == ContractEffectKind::ContractCall;
                                })
            || !effects_match_contract(output.value(), contract_id, kind, owner_id)) {
            return std::unexpected(
                failure(ContractError::InvalidResponse, "Contract initialization emitted an invalid effect"));
        }
        auto initial_revision = revision(output.value(), nullptr, block, owner_id);
        if (!initial_revision.has_value()) {
            return std::unexpected(initial_revision.error());
        }

        ContractRecord record {
            .contract_id    = std::move(contract_id),
            .owner_id       = std::move(owner_id),
            .kind           = std::move(kind),
            .language       = language.value_or(std::string()),
            .active_version = 1,
            .versions       = { ContractVersion {
                      .version              = 1,
                      .module_hash          = content_hash(module),
                      .previous_module_hash = {},
                      .module               = { module.begin(), module.end() },
                      .revisions            = { std::move(initial_revision.value()) },
            } },
        };
        return PreparedContractChange {
            .kind       = ContractChangeKind::Create,
            .record     = std::move(record),
            .output     = std::move(output.value()),
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
        std::uint64_t                 block,
        const VerifiedInputs         &verified) {
        std::unique_lock                lock(mutex_);
        std::vector<std::string>        stack { std::string(contract_id) };
        std::unordered_set<std::string> touched { std::string(contract_id) };
        std::uint32_t                   call_count = 1;
        return prepare_call_unlocked(contract_id,
                                     sender_id,
                                     sender_id,
                                     method,
                                     arguments,
                                     block,
                                     0,
                                     stack,
                                     touched,
                                     call_count,
                                     verified);
    }

    std::expected<PreparedContractChange, ContractFailure> ContractManager::prepare_call_unlocked(
        std::string_view                 contract_id,
        std::string_view                 sender_id,
        std::string_view                 caller_id,
        std::string_view                 method,
        std::span<const std::uint8_t>    arguments,
        std::uint64_t                    block,
        std::uint32_t                    depth,
        std::vector<std::string>        &stack,
        std::unordered_set<std::string> &touched,
        std::uint32_t                   &call_count,
        const VerifiedInputs            &verified) {
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
        auto       output   = evaluate(version.module,
                               sender_id,
                               method,
                               arguments,
                               previous.state,
                               block,
                               contract_id,
                               caller_id,
                               verified,
                               depth);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        if (!effects_match_contract(output.value(), contract_id, record.kind, sender_id, verified)) {
            return std::unexpected(
                failure(ContractError::InvalidResponse, "Contract emitted an effect for another authority"));
        }
        if (output.value().state == previous.state && output.value().effects.empty()) {
            auto expected_version = version.version;
            return PreparedContractChange {
                .kind                = ContractChangeKind::ReadOnly,
                .record              = std::move(record),
                .output              = std::move(output.value()),
                .expected_version    = expected_version,
                .expected_state_hash = previous.state_hash,
            };
        }
        auto next = revision(output.value(), &previous, block, std::string(sender_id));
        if (!next.has_value()) {
            return std::unexpected(next.error());
        }
        version.revisions.push_back(std::move(next.value()));
        auto                   expected_version = version.version;
        PreparedContractChange change {
            .kind                = ContractChangeKind::Replace,
            .record              = std::move(record),
            .output              = std::move(output.value()),
            .expected_version    = expected_version,
            .expected_state_hash = previous.state_hash,
            .checkpoint = version.revisions.back().checkpoint_revision == version.revisions.back().revision,
        };
        for (const auto &effect : change.output.effects) {
            if (effect.kind != ContractEffectKind::ContractCall) {
                continue;
            }
            if (call_count >= ContractMaximumCalls || depth >= ContractMaximumCallDepth) {
                return std::unexpected(
                    failure(ContractError::CallDepthExceeded, "Contract call graph exceeds its limit"));
            }
            if (std::ranges::find(stack, effect.target) != stack.end() || touched.contains(effect.target)) {
                return std::unexpected(
                    failure(ContractError::CallCycle, "Contract call graph has a cycle or repeated target"));
            }
            ++call_count;
            stack.push_back(effect.target);
            touched.insert(effect.target);
            auto child = prepare_call_unlocked(effect.target,
                                               sender_id,
                                               contract_id,
                                               effect.operation,
                                               effect.arguments,
                                               block,
                                               depth + 1,
                                               stack,
                                               touched,
                                               call_count,
                                               verified);
            stack.pop_back();
            if (!child.has_value()) {
                return std::unexpected(child.error());
            }
            if (child->kind == ContractChangeKind::ReadOnly) {
                return std::unexpected(
                    failure(ContractError::InvalidResponse, "Contract call effects must change the target state"));
            }
            change.children.push_back(std::move(*child));
        }
        return change;
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
        auto output = evaluate(version.module, sender_id, method, arguments, previous.state, block, contract_id);
        if (!output.has_value()) {
            return std::unexpected(output.error());
        }
        if (output->state != previous.state || !output->effects.empty()) {
            return std::unexpected(
                failure(ContractError::InvalidArguments, "The method changes state or emits effects"));
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
        if (is_system_token_kind(record.kind)) {
            auto language = module_language(module);
            if (!language.has_value() || *language != record.language) {
                return std::unexpected(
                    failure(ContractError::UpgradeDenied, "A token update must use its original language"));
            }
        }
        auto      &current                 = active_version(record);
        const auto previous                = latest_revision(current);
        const auto new_module_hash         = content_hash(module);
        auto       authorization_arguments = Codec::encode_string(new_module_hash);
        auto       authorization           = evaluate(current.module,
                                      sender_id,
                                      "authorize_upgrade",
                                      authorization_arguments,
                                      previous.state,
                                      block,
                                      contract_id);
        if (!authorization.has_value()) {
            return std::unexpected(failure(ContractError::UpgradeDenied, authorization.error().detail));
        }
        if (authorization->state != previous.state || !authorization->effects.empty()) {
            return std::unexpected(
                failure(ContractError::UpgradeDenied, "Upgrade authorization changed contract state"));
        }
        auto migration =
            evaluate(module, sender_id, "migrate", migration_arguments, previous.state, block, contract_id);
        if (!migration.has_value()) {
            return std::unexpected(migration.error());
        }
        if (!migration->effects.empty()) {
            return std::unexpected(
                failure(ContractError::InvalidResponse, "Contract migration cannot emit effects"));
        }
        StateRevision migrated {
            .revision             = previous.revision + 1,
            .block                = block,
            .previous_hash        = previous.state_hash,
            .state_hash           = content_hash(migration->state),
            .transaction_hash     = {},
            .author_id            = std::string(sender_id),
            .state                = migration->state,
            .checkpoint_revision  = previous.revision + 1,
            .checkpoint_block     = block,
            .checkpoint_hash      = content_hash(migration->state),
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
        std::unique_lock                            lock(mutex_);
        std::vector<const PreparedContractChange *> changes;
        const auto collect = [&](const auto &self, const PreparedContractChange &current) -> void {
            changes.push_back(&current);
            for (const auto &child : current.children) {
                self(self, child);
            }
        };
        collect(collect, change);
        std::ranges::sort(changes, {}, [](const auto *current) {
            return current->record.contract_id;
        });
        for (const auto *current : changes) {
            auto staged = storage_->stage(current->record);
            if (!staged.has_value()) {
                return staged;
            }
        }
        return {};
    }

    std::expected<ContractReceipt, ContractFailure> ContractManager::commit(PreparedContractChange change,
                                                                            std::string transaction_hash) {
        std::unique_lock                      lock(mutex_);
        std::vector<PreparedContractChange *> changes;
        const auto collect = [&](const auto &self, PreparedContractChange &current) -> void {
            changes.push_back(&current);
            for (auto &child : current.children) {
                self(self, child);
            }
        };
        collect(collect, change);

        for (const auto *current : changes) {
            if (current->kind != ContractChangeKind::Replace) {
                continue;
            }
            const auto stored = storage_->load(current->record.contract_id);
            if (!stored.has_value()) {
                return std::unexpected(stored.error());
            }
            const auto &stored_version = active_version(*stored);
            if (stored_version.version != current->expected_version || stored_version.revisions.empty()
                || latest_revision(stored_version).state_hash != current->expected_state_hash) {
                return std::unexpected(
                    failure(ContractError::Conflict, "Contract state changed before graph commit"));
            }
        }

        std::ranges::sort(changes, {}, [](const auto *current) {
            return current->record.contract_id;
        });
        for (auto *current : changes) {
            auto &version = active_version(current->record);
            if (!version.revisions.empty() && current->kind != ContractChangeKind::ReadOnly) {
                version.revisions.back().transaction_hash = transaction_hash;
                if (current->checkpoint) {
                    version.revisions.back().checkpoint_transaction_hash = transaction_hash;
                }
            }

            std::expected<void, ContractFailure> saved;
            switch (current->kind) {
            case ContractChangeKind::Create:
                saved = storage_->create(current->record);
                break;
            case ContractChangeKind::Replace:
                saved =
                    storage_->replace(current->record, current->expected_version, current->expected_state_hash);
                break;
            case ContractChangeKind::ReadOnly:
                continue;
            }
            if (!saved.has_value()) {
                return std::unexpected(saved.error());
            }
        }
        return receipt(change.record, change.output);
    }

    std::expected<ContractRecord, ContractFailure> ContractManager::inspect(std::string_view contract_id) const {
        std::shared_lock lock(mutex_);
        return storage_->load(contract_id);
    }

} // namespace ExtraChain::Contracts
