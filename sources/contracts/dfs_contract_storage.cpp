/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "contracts/dfs_contract_storage.h"

#include <algorithm>

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include "chain/dag.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_transaction.h"
#include <fmt/format.h>
#include <msgpack.hpp>

#include "dfs/dfs_controller.h"
#include "dfs/dfs_utils.h"
#include "utils/exc_utils.h"

namespace ExtraChain::Contracts {
    namespace {

        struct RevisionReference {
            std::uint32_t version  = 0;
            std::uint64_t revision = 0;
            std::uint64_t block    = 0;
            std::string   previous_hash;
            std::string   state_hash;
            std::string   transaction_hash;
            std::string   author_id;
            std::string   state_file_id;

            MSGPACK_DEFINE(version,
                           revision,
                           block,
                           previous_hash,
                           state_hash,
                           transaction_hash,
                           author_id,
                           state_file_id)
        };

        struct VersionReference {
            std::uint32_t version = 0;
            std::string   module_hash;
            std::string   previous_module_hash;
            std::string   module_file_id;

            MSGPACK_DEFINE(version, module_hash, previous_module_hash, module_file_id)
        };

        struct ContractHeadCache {
            std::uint32_t                 schema = 2;
            std::string                   contract_id;
            std::string                   owner_id;
            std::string                   kind;
            std::uint32_t                 active_version = 1;
            std::vector<VersionReference> versions;
            RevisionReference             head;
            RevisionReference             checkpoint;
            std::vector<std::uint8_t>     state;

            MSGPACK_DEFINE(schema, contract_id, owner_id, kind, active_version, versions, head, checkpoint, state)
        };

        ContractFailure failure(ContractError error, std::string detail) {
            return { error, std::move(detail) };
        }

        std::string content_hash(std::span<const std::uint8_t> value) {
            QCryptographicHash hasher(QCryptographicHash::Blake2b_256);
            hasher.addData(QByteArrayView(reinterpret_cast<const char *>(value.data()),
                                          static_cast<qsizetype>(value.size())));
            return hasher.result().toHex().toStdString();
        }

        std::string prefix(std::string_view value) {
            return std::string(value.substr(0, std::min<std::size_t>(value.size(), 12)));
        }

        QString head_path(const ActorId &contract_id) {
            return QDir::current().filePath(
                QStringLiteral("contract-heads/%1.msgpack").arg(contract_id.toQString()));
        }

        bool write_head(const ActorId &contract_id, const ContractHeadCache &head) {
            QDir directory(QDir::current().filePath(QStringLiteral("contract-heads")));
            if (!directory.exists() && !QDir::current().mkpath(QStringLiteral("contract-heads"))) {
                return false;
            }
            msgpack::sbuffer buffer;
            msgpack::pack(buffer, head);
            QSaveFile file(head_path(contract_id));
            return file.open(QIODevice::WriteOnly)
                   && file.write(buffer.data(), static_cast<qint64>(buffer.size()))
                          == static_cast<qint64>(buffer.size())
                   && file.commit();
        }

        std::expected<std::vector<std::uint8_t>, ContractFailure> read_file(DfsController   *dfs,
                                                                            const ActorId   &owner_id,
                                                                            std::string_view file_id) {
            auto content = Dfs::Tables::DirsFile::ActorSpace::get_file_content(owner_id, std::string(file_id));
            if (!content.has_value()) {
                if (dfs != nullptr) {
                    dfs->request_file(owner_id, std::string(file_id));
                }
                return std::unexpected(failure(ContractError::StorageError, "Contract DFS file is not available"));
            }
            return *content;
        }

        std::expected<Dfs::DirRow, ContractFailure> store_file(DfsController                *dfs,
                                                               const ActorId                &contract_id,
                                                               const ActorId                &author_id,
                                                               std::span<const std::uint8_t> data,
                                                               std::string                   name) {
            auto result = dfs->store_data_as_file(contract_id,
                                                  author_id,
                                                  { data.begin(), data.end() },
                                                  Dfs::Basic::TEMPLATE_CONTRACTS,
                                                  name,
                                                  Dfs::DataSecurity::Public);
            if (result.has_value()) {
                return *result;
            }
            auto existing = dfs->read_file_status(contract_id, name, Dfs::Basic::TEMPLATE_CONTRACTS);
            if (existing.has_value() && existing->actor_id == author_id) {
                auto content = read_file(dfs, contract_id, existing->file_id);
                if (content.has_value() && std::ranges::equal(*content, data)) {
                    return *existing;
                }
            }
            return std::unexpected(failure(ContractError::StorageError, "Cannot store contract data in DFS"));
        }

        std::expected<ContractRecord, ContractFailure> save_head(DfsController *dfs,
                                                                 const ActorId &contract_id,
                                                                 ContractRecord record) {
            if (record.active_version == 0 || record.active_version > record.versions.size()) {
                return std::unexpected(failure(ContractError::StorageError, "Contract version is invalid"));
            }
            auto &active = record.versions.at(record.active_version - 1);
            if (active.revisions.empty()) {
                return std::unexpected(failure(ContractError::StorageError, "Contract state is missing"));
            }
            for (auto &version : record.versions) {
                if (version.module_storage_id.empty()) {
                    const auto module_name = fmt::format("contract-module-v{:06}-{}.wasm",
                                                         version.version,
                                                         prefix(version.module_hash));
                    auto row = dfs->read_file_status(contract_id, module_name, Dfs::Basic::TEMPLATE_CONTRACTS);
                    if (!row.has_value()) {
                        return std::unexpected(failure(ContractError::StorageError, "Contract module is missing"));
                    }
                    version.module_storage_id = row->file_id;
                }
            }
            auto &revision = active.revisions.back();
            if (revision.checkpoint_storage_id.empty()) {
                const auto checkpoint_name = fmt::format("contract-checkpoint-v{:06}-r{:012}-{}.msgpack",
                                                         record.active_version,
                                                         revision.checkpoint_revision,
                                                         prefix(revision.checkpoint_hash));
                auto row = dfs->read_file_status(contract_id, checkpoint_name, Dfs::Basic::TEMPLATE_CONTRACTS);
                if (!row.has_value()) {
                    return std::unexpected(failure(ContractError::StorageError, "Contract checkpoint is missing"));
                }
                revision.checkpoint_storage_id = row->file_id;
            }
            ContractHeadCache cache {
                .contract_id    = record.contract_id,
                .owner_id       = record.owner_id,
                .kind           = record.kind,
                .active_version = record.active_version,
                .head =
                    RevisionReference {
                        .version          = record.active_version,
                        .revision         = revision.revision,
                        .block            = revision.block,
                        .previous_hash    = revision.previous_hash,
                        .state_hash       = revision.state_hash,
                        .transaction_hash = revision.transaction_hash,
                        .author_id        = revision.author_id,
                    },
                .checkpoint =
                    RevisionReference {
                        .version          = record.active_version,
                        .revision         = revision.checkpoint_revision,
                        .block            = revision.checkpoint_block,
                        .state_hash       = revision.checkpoint_hash,
                        .transaction_hash = revision.checkpoint_transaction_hash,
                        .author_id        = revision.checkpoint_author_id,
                        .state_file_id    = revision.checkpoint_storage_id,
                    },
                .state = revision.state,
            };
            cache.versions.reserve(record.versions.size());
            for (const auto &version : record.versions) {
                cache.versions.push_back(VersionReference {
                    .version              = version.version,
                    .module_hash          = version.module_hash,
                    .previous_module_hash = version.previous_module_hash,
                    .module_file_id       = version.module_storage_id,
                });
            }
            if (!write_head(contract_id, cache)) {
                return std::unexpected(failure(ContractError::StorageError, "Cannot save contract head"));
            }
            retain_current_contract_state(record);
            return record;
        }

        std::optional<Transaction> find_transaction(Dag             *dag,
                                                    std::uint64_t    block,
                                                    std::string_view transaction_hash) {
            if (dag == nullptr) {
                return std::nullopt;
            }
            auto section = dag->read_section(SectionId(block));
            if (!section.has_value()) {
                dag->request_contract_section(SectionId(block));
                return std::nullopt;
            }
            auto transaction = std::ranges::find_if(section->transactions, [&](const Transaction &candidate) {
                return candidate.hash() == transaction_hash;
            });
            if (transaction == section->transactions.end()) {
                return std::nullopt;
            }
            return *transaction;
        }

        std::expected<ContractRecord, ContractFailure> load_head(DfsController *dfs,
                                                                 Dag           *dag,
                                                                 const ActorId &contract_id) {
            QFile file(head_path(contract_id));
            if (!file.open(QIODevice::ReadOnly)) {
                return std::unexpected(failure(ContractError::NotFound, "Contract head does not exist"));
            }
            const auto bytes = file.readAll();
            try {
                auto object = msgpack::unpack(bytes.constData(), static_cast<std::size_t>(bytes.size()));
                auto cache  = object.get().as<ContractHeadCache>();
                if (cache.schema != 2 || cache.contract_id != contract_id.to_string() || cache.owner_id.empty()
                    || cache.kind.empty() || cache.active_version == 0
                    || cache.active_version != cache.versions.size() || cache.head.version != cache.active_version
                    || cache.checkpoint.version != cache.active_version || cache.head.revision == 0
                    || cache.head.state_hash != content_hash(cache.state) || cache.head.transaction_hash.empty()
                    || cache.checkpoint.revision == 0 || cache.checkpoint.revision > cache.head.revision
                    || cache.checkpoint.transaction_hash.empty() || cache.checkpoint.state_file_id.empty()) {
                    return std::unexpected(failure(ContractError::StorageError, "Contract head is invalid"));
                }
                std::string previous_module_hash;
                for (std::size_t index = 0; index < cache.versions.size(); ++index) {
                    const auto &version = cache.versions[index];
                    if (version.version != index + 1 || version.module_hash.size() != 64
                        || version.previous_module_hash != previous_module_hash
                        || version.module_file_id.empty()) {
                        return std::unexpected(
                            failure(ContractError::StorageError, "Contract module history is invalid"));
                    }
                    previous_module_hash = version.module_hash;
                }
                const auto transaction = find_transaction(dag, cache.head.block, cache.head.transaction_hash);
                if (!transaction.has_value() || transaction->sender().to_string() != cache.head.author_id
                    || !transaction->meta().has_value()) {
                    return std::unexpected(
                        failure(ContractError::StorageError, "Contract head transaction is not approved"));
                }
                const auto metadata = Json::deserialize<ContractTransactionData>(*transaction->meta());
                const auto head_transition = metadata.has_value()
                                                 ? std::ranges::find(metadata->transitions,
                                                                     cache.contract_id,
                                                                     &ContractTransitionData::contract_id)
                                                 : std::vector<ContractTransitionData>::const_iterator {};
                const bool root_head       = transaction->receiver() == contract_id;
                const bool head_matches =
                    metadata.has_value() && metadata->schema == 3
                    && ((root_head && metadata->revision == cache.head.revision
                         && metadata->state_hash == cache.head.state_hash
                         && metadata->previous_state_hash == cache.head.previous_hash
                         && metadata->version == cache.active_version && metadata->kind == cache.kind
                         && metadata->module_hash == cache.versions.back().module_hash
                         && metadata->checkpoint_revision == cache.checkpoint.revision)
                        || (!root_head && head_transition != metadata->transitions.end()
                            && head_transition->revision == cache.head.revision
                            && head_transition->state_hash == cache.head.state_hash
                            && head_transition->previous_state_hash == cache.head.previous_hash
                            && head_transition->version == cache.active_version
                            && head_transition->kind == cache.kind
                            && head_transition->module_hash == cache.versions.back().module_hash
                            && head_transition->checkpoint_revision == cache.checkpoint.revision));
                if (!head_matches) {
                    return std::unexpected(
                        failure(ContractError::StorageError, "Contract head does not match the approved chain"));
                }
                const auto checkpoint_transaction =
                    find_transaction(dag, cache.checkpoint.block, cache.checkpoint.transaction_hash);
                if (!checkpoint_transaction.has_value()
                    || checkpoint_transaction->sender().to_string() != cache.checkpoint.author_id
                    || !checkpoint_transaction->meta().has_value()) {
                    return std::unexpected(
                        failure(ContractError::StorageError, "Contract checkpoint is not approved"));
                }
                const auto checkpoint_metadata =
                    Json::deserialize<ContractTransactionData>(*checkpoint_transaction->meta());
                const auto checkpoint_transition = checkpoint_metadata.has_value()
                                                       ? std::ranges::find(checkpoint_metadata->transitions,
                                                                           cache.contract_id,
                                                                           &ContractTransitionData::contract_id)
                                                       : std::vector<ContractTransitionData>::const_iterator {};
                const bool root_checkpoint       = checkpoint_transaction->receiver() == contract_id;
                const bool checkpoint_matches =
                    checkpoint_metadata.has_value() && checkpoint_metadata->schema == 3
                    && ((root_checkpoint && checkpoint_metadata->checkpoint
                         && checkpoint_metadata->revision == cache.checkpoint.revision
                         && checkpoint_metadata->state_hash == cache.checkpoint.state_hash
                         && checkpoint_metadata->version == cache.checkpoint.version
                         && checkpoint_metadata->module_hash == cache.versions.back().module_hash
                         && checkpoint_metadata->kind == cache.kind)
                        || (!root_checkpoint && checkpoint_transition != checkpoint_metadata->transitions.end()
                            && checkpoint_transition->checkpoint
                            && checkpoint_transition->revision == cache.checkpoint.revision
                            && checkpoint_transition->state_hash == cache.checkpoint.state_hash
                            && checkpoint_transition->version == cache.checkpoint.version
                            && checkpoint_transition->module_hash == cache.versions.back().module_hash
                            && checkpoint_transition->kind == cache.kind));
                if (!checkpoint_matches) {
                    return std::unexpected(failure(ContractError::StorageError,
                                                   "Contract checkpoint does not match the approved chain"));
                }
                auto checkpoint_state = read_file(dfs, contract_id, cache.checkpoint.state_file_id);
                if (!checkpoint_state.has_value()
                    || content_hash(*checkpoint_state) != cache.checkpoint.state_hash) {
                    return std::unexpected(
                        failure(ContractError::StorageError, "Contract checkpoint content is invalid"));
                }

                ContractRecord record {
                    .contract_id    = cache.contract_id,
                    .owner_id       = cache.owner_id,
                    .kind           = cache.kind,
                    .active_version = cache.active_version,
                };
                record.versions.reserve(cache.versions.size());
                for (const auto &reference : cache.versions) {
                    std::vector<std::uint8_t> module;
                    if (reference.version == cache.active_version) {
                        auto loaded = read_file(dfs, contract_id, reference.module_file_id);
                        if (!loaded.has_value() || content_hash(*loaded) != reference.module_hash) {
                            return std::unexpected(
                                failure(ContractError::StorageError, "Active contract module is unavailable"));
                        }
                        module = std::move(*loaded);
                    }
                    ContractVersion version {
                        .version              = reference.version,
                        .module_hash          = reference.module_hash,
                        .previous_module_hash = reference.previous_module_hash,
                        .module_storage_id    = reference.module_file_id,
                        .module               = std::move(module),
                    };
                    if (reference.version == cache.active_version) {
                        version.revisions.push_back(StateRevision {
                            .revision                    = cache.head.revision,
                            .block                       = cache.head.block,
                            .previous_hash               = cache.head.previous_hash,
                            .state_hash                  = cache.head.state_hash,
                            .transaction_hash            = cache.head.transaction_hash,
                            .author_id                   = cache.head.author_id,
                            .storage_id                  = cache.head.state_file_id,
                            .state                       = cache.state,
                            .checkpoint_revision         = cache.checkpoint.revision,
                            .checkpoint_block            = cache.checkpoint.block,
                            .checkpoint_hash             = cache.checkpoint.state_hash,
                            .checkpoint_transaction_hash = cache.checkpoint.transaction_hash,
                            .checkpoint_storage_id       = cache.checkpoint.state_file_id,
                            .checkpoint_author_id        = cache.checkpoint.author_id,
                        });
                    }
                    record.versions.push_back(std::move(version));
                }
                return record;
            } catch (const std::exception &) {
                return std::unexpected(failure(ContractError::StorageError, "Contract head cannot be decoded"));
            }
        }

        std::expected<ContractRecord, ContractFailure> replay_tail(ContractRecord record, Dag *dag) {
            if (dag == nullptr || record.active_version == 0 || record.active_version > record.versions.size()) {
                return record;
            }
            auto &version = record.versions.at(record.active_version - 1);
            if (version.revisions.empty() || version.module.empty()) {
                return std::unexpected(
                    failure(ContractError::StorageError, "Contract checkpoint cannot be replayed"));
            }
            auto            current = version.revisions.back();
            ContractManager evaluator;
            const auto      last = dag->current_section().to_int();
            if (!last.has_value()) {
                return record;
            }
            for (std::uint64_t section_number = current.block; section_number <= static_cast<std::uint64_t>(*last);
                 ++section_number) {
                const auto section = dag->read_section(SectionId(section_number));
                if (!section.has_value()) {
                    continue;
                }
                for (const auto &transaction : section->transactions) {
                    if (transaction.type() != TransactionType::ContractCall || !transaction.meta().has_value()) {
                        continue;
                    }
                    const auto metadata = Json::deserialize<ContractTransactionData>(*transaction.meta());
                    if (!metadata.has_value() || metadata->schema != 3) {
                        continue;
                    }
                    ContractTransitionData invocation;
                    if (transaction.receiver().to_string() == record.contract_id) {
                        invocation = ContractTransitionData {
                            .contract_id         = record.contract_id,
                            .caller_contract_id  = transaction.sender().to_string(),
                            .kind                = metadata->kind,
                            .method              = metadata->method,
                            .arguments_base64    = metadata->arguments_base64,
                            .module_hash         = metadata->module_hash,
                            .previous_state_hash = metadata->previous_state_hash,
                            .state_hash          = metadata->state_hash,
                            .effects_hash        = metadata->effects_hash,
                            .version             = metadata->version,
                            .revision            = metadata->revision,
                            .checkpoint          = metadata->checkpoint,
                            .checkpoint_revision = metadata->checkpoint_revision,
                        };
                    } else {
                        const auto nested = std::ranges::find(metadata->transitions,
                                                              record.contract_id,
                                                              &ContractTransitionData::contract_id);
                        if (nested == metadata->transitions.end()) {
                            continue;
                        }
                        invocation = *nested;
                    }
                    if (invocation.revision <= current.revision) {
                        continue;
                    }
                    if (invocation.revision != current.revision + 1 || invocation.version != version.version
                        || invocation.module_hash != version.module_hash
                        || invocation.previous_state_hash != current.state_hash) {
                        return std::unexpected(
                            failure(ContractError::StorageError, "Contract replay sequence is invalid"));
                    }
                    auto arguments = Utils::from_base64<std::vector<std::uint8_t>>(invocation.arguments_base64);
                    if (!arguments.has_value()) {
                        return std::unexpected(
                            failure(ContractError::StorageError, "Contract replay arguments are invalid"));
                    }
                    auto output = evaluator.evaluate(version.module,
                                                     transaction.sender().to_string(),
                                                     invocation.method,
                                                     *arguments,
                                                     current.state,
                                                     section_number,
                                                     record.contract_id,
                                                     invocation.caller_contract_id,
                                                     metadata->verified_inputs);
                    if (!output.has_value() || content_hash(output->state) != invocation.state_hash
                        || Codec::effect_hash(output->effects) != invocation.effects_hash) {
                        return std::unexpected(
                            failure(ContractError::StorageError, "Contract replay result is invalid"));
                    }
                    current = StateRevision {
                        .revision                    = invocation.revision,
                        .block                       = section_number,
                        .previous_hash               = invocation.previous_state_hash,
                        .state_hash                  = invocation.state_hash,
                        .transaction_hash            = transaction.hash(),
                        .author_id                   = transaction.sender().to_string(),
                        .state                       = std::move(output->state),
                        .checkpoint_revision         = invocation.checkpoint_revision,
                        .checkpoint_block            = current.checkpoint_block,
                        .checkpoint_hash             = current.checkpoint_hash,
                        .checkpoint_transaction_hash = current.checkpoint_transaction_hash,
                        .checkpoint_storage_id       = current.checkpoint_storage_id,
                        .checkpoint_author_id        = current.checkpoint_author_id,
                    };
                    if (invocation.checkpoint) {
                        current.checkpoint_revision         = current.revision;
                        current.checkpoint_block            = current.block;
                        current.checkpoint_hash             = current.state_hash;
                        current.checkpoint_transaction_hash = current.transaction_hash;
                        current.checkpoint_storage_id.clear();
                        current.checkpoint_author_id = current.author_id;
                    }
                }
            }
            version.revisions.assign(1, std::move(current));
            return record;
        }

        std::expected<ContractRecord, ContractFailure> load_checkpoint_from_dag(DfsController *dfs,
                                                                                Dag           *dag,
                                                                                const ActorId &contract_id) {
            if (dag == nullptr) {
                return std::unexpected(failure(ContractError::NotFound, "Contract checkpoint does not exist"));
            }
            std::string                      owner_id;
            std::string                      kind;
            std::vector<VersionReference>    versions;
            std::optional<RevisionReference> checkpoint;
            const auto                       first = dag->first_saved_section();
            const auto                       last  = dag->current_section();
            if (first < SectionId(0) || last < first) {
                return std::unexpected(failure(ContractError::NotFound, "Contract checkpoint does not exist"));
            }
            for (SectionId section_id = first; section_id <= last; ++section_id) {
                const auto section = dag->read_section(section_id);
                if (!section.has_value()) {
                    continue;
                }
                for (const auto &transaction : section->transactions) {
                    if (!transaction.meta().has_value()) {
                        continue;
                    }
                    const auto metadata = Json::deserialize<ContractTransactionData>(*transaction.meta());
                    if (!metadata.has_value() || metadata->schema != 3) {
                        continue;
                    }
                    const bool root_transaction = transaction.receiver() == contract_id;
                    const auto nested           = std::ranges::find(metadata->transitions,
                                                          contract_id.to_string(),
                                                          &ContractTransitionData::contract_id);
                    if (!root_transaction && nested == metadata->transitions.end()) {
                        continue;
                    }
                    if (root_transaction && transaction.type() == TransactionType::ContractDeploy) {
                        owner_id = transaction.sender().to_string();
                        kind     = metadata->kind;
                        versions.clear();
                        versions.push_back(VersionReference {
                            .version     = 1,
                            .module_hash = metadata->module_hash,
                        });
                    } else if (root_transaction && transaction.type() == TransactionType::ContractUpgrade) {
                        if (versions.size() + 1 != metadata->version) {
                            return std::unexpected(
                                failure(ContractError::StorageError, "Contract module history is invalid"));
                        }
                        versions.push_back(VersionReference {
                            .version              = metadata->version,
                            .module_hash          = metadata->module_hash,
                            .previous_module_hash = versions.back().module_hash,
                        });
                    }
                    const auto checkpoint_enabled  = root_transaction ? metadata->checkpoint : nested->checkpoint;
                    const auto checkpoint_revision = root_transaction ? metadata->revision : nested->revision;
                    if (checkpoint_enabled
                        && (!checkpoint.has_value() || checkpoint_revision > checkpoint->revision)) {
                        checkpoint = RevisionReference {
                            .version  = root_transaction ? metadata->version : nested->version,
                            .revision = checkpoint_revision,
                            .block    = static_cast<std::uint64_t>(section_id.to_int().value_or(0)),
                            .previous_hash =
                                root_transaction ? metadata->previous_state_hash : nested->previous_state_hash,
                            .state_hash       = root_transaction ? metadata->state_hash : nested->state_hash,
                            .transaction_hash = transaction.hash(),
                            .author_id        = transaction.sender().to_string(),
                        };
                    }
                }
            }
            if (owner_id.empty() || kind.empty() || versions.empty() || !checkpoint.has_value()) {
                return std::unexpected(failure(ContractError::NotFound, "Contract checkpoint does not exist"));
            }
            const auto active_version = static_cast<std::uint32_t>(versions.size());
            if (checkpoint->version != active_version) {
                return std::unexpected(
                    failure(ContractError::StorageError, "Current contract version has no checkpoint"));
            }
            for (auto &version : versions) {
                const auto name =
                    fmt::format("contract-module-v{:06}-{}.wasm", version.version, prefix(version.module_hash));
                auto row = dfs->read_file_status(contract_id, name, Dfs::Basic::TEMPLATE_CONTRACTS);
                if (!row.has_value()) {
                    return std::unexpected(
                        failure(ContractError::StorageError, "Contract module is not available"));
                }
                version.module_file_id = row->file_id;
            }
            const auto checkpoint_name = fmt::format("contract-checkpoint-v{:06}-r{:012}-{}.msgpack",
                                                     checkpoint->version,
                                                     checkpoint->revision,
                                                     prefix(checkpoint->state_hash));
            auto       checkpoint_row =
                dfs->read_file_status(contract_id, checkpoint_name, Dfs::Basic::TEMPLATE_CONTRACTS);
            if (!checkpoint_row.has_value()) {
                return std::unexpected(
                    failure(ContractError::StorageError, "Contract checkpoint is not available"));
            }
            checkpoint->state_file_id = checkpoint_row->file_id;
            auto state                = read_file(dfs, contract_id, checkpoint->state_file_id);
            auto module               = read_file(dfs, contract_id, versions.back().module_file_id);
            if (!state.has_value() || content_hash(*state) != checkpoint->state_hash || !module.has_value()
                || content_hash(*module) != versions.back().module_hash) {
                return std::unexpected(
                    failure(ContractError::StorageError, "Contract checkpoint content is invalid"));
            }

            ContractRecord record {
                .contract_id    = contract_id.to_string(),
                .owner_id       = owner_id,
                .kind           = kind,
                .active_version = active_version,
            };
            record.versions.reserve(versions.size());
            for (auto &reference : versions) {
                ContractVersion version {
                    .version              = reference.version,
                    .module_hash          = reference.module_hash,
                    .previous_module_hash = reference.previous_module_hash,
                    .module_storage_id    = reference.module_file_id,
                };
                if (reference.version == active_version) {
                    version.module = *module;
                    version.revisions.push_back(StateRevision {
                        .revision                    = checkpoint->revision,
                        .block                       = checkpoint->block,
                        .previous_hash               = checkpoint->previous_hash,
                        .state_hash                  = checkpoint->state_hash,
                        .transaction_hash            = checkpoint->transaction_hash,
                        .author_id                   = checkpoint->author_id,
                        .storage_id                  = checkpoint->state_file_id,
                        .state                       = *state,
                        .checkpoint_revision         = checkpoint->revision,
                        .checkpoint_block            = checkpoint->block,
                        .checkpoint_hash             = checkpoint->state_hash,
                        .checkpoint_transaction_hash = checkpoint->transaction_hash,
                        .checkpoint_storage_id       = checkpoint->state_file_id,
                        .checkpoint_author_id        = checkpoint->author_id,
                    });
                }
                record.versions.push_back(std::move(version));
            }
            return record;
        }

    } // namespace

    DfsContractStorage::DfsContractStorage(DfsController *dfs, Dag *dag)
        : dfs_(dfs)
        , dag_(dag) {
    }

    std::expected<ContractRecord, ContractFailure> DfsContractStorage::load(
        std::string_view contract_id_value) const {
        std::scoped_lock lock(mutex_);
        if (dfs_ == nullptr) {
            return std::unexpected(failure(ContractError::StorageError, "DFS is not initialized"));
        }
        auto contract_id = ActorId::create(std::string(contract_id_value));
        if (!contract_id.has_value()) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Contract ID is invalid"));
        }
        if (auto cached = heads_.find(contract_id->to_string()); cached != heads_.end()) {
            return cached->second;
        }
        if (auto head = load_head(dfs_, dag_, *contract_id); head.has_value()) {
            heads_.insert_or_assign(contract_id->to_string(), *head);
            return head;
        }
        auto checkpoint = load_checkpoint_from_dag(dfs_, dag_, *contract_id);
        if (!checkpoint.has_value()) {
            return std::unexpected(checkpoint.error());
        }
        auto replayed = replay_tail(std::move(*checkpoint), dag_);
        if (!replayed.has_value()) {
            return std::unexpected(replayed.error());
        }
        auto saved = save_head(dfs_, *contract_id, std::move(*replayed));
        if (!saved.has_value()) {
            return std::unexpected(saved.error());
        }
        heads_.insert_or_assign(contract_id->to_string(), *saved);
        return saved;
    }

    std::expected<void, ContractFailure> DfsContractStorage::stage_artifacts(const ContractRecord &record) const {
        auto contract_id = ActorId::create(record.contract_id);
        auto owner_id    = ActorId::create(record.owner_id);
        if (!contract_id.has_value() || !owner_id.has_value()) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Contract owner or ID is invalid"));
        }

        for (const auto &version : record.versions) {
            std::string module_file_id = version.module_storage_id;
            if (module_file_id.empty()) {
                auto module_name =
                    fmt::format("contract-module-v{:06}-{}.wasm", version.version, prefix(version.module_hash));
                auto module_row = store_file(dfs_, *contract_id, *owner_id, version.module, module_name);
                if (!module_row.has_value()) {
                    return std::unexpected(module_row.error());
                }
                module_file_id = module_row->file_id;
            }
            if (module_file_id.empty()) {
                return std::unexpected(failure(ContractError::StorageError, "Contract module file is missing"));
            }
            if (version.version == record.active_version) {
                if (version.revisions.empty()) {
                    return std::unexpected(failure(ContractError::StorageError, "Active contract has no state"));
                }
                const auto &revision = version.revisions.back();
                if (revision.checkpoint_revision == revision.revision) {
                    auto state_name      = fmt::format("contract-checkpoint-v{:06}-r{:012}-{}.msgpack",
                                                  version.version,
                                                  revision.revision,
                                                  prefix(revision.state_hash));
                    auto revision_author = ActorId::create(revision.author_id);
                    if (!revision_author.has_value()) {
                        return std::unexpected(
                            failure(ContractError::InvalidOwner, "Contract revision author is invalid"));
                    }
                    auto state_row = store_file(dfs_, *contract_id, *revision_author, revision.state, state_name);
                    if (!state_row.has_value()) {
                        return std::unexpected(state_row.error());
                    }
                }
            }
        }
        return {};
    }

    std::expected<void, ContractFailure> DfsContractStorage::create(const ContractRecord &record) {
        std::scoped_lock lock(mutex_);
        auto             contract_id = ActorId::create(record.contract_id);
        if (!contract_id.has_value()) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Contract ID is invalid"));
        }
        if (heads_.contains(contract_id->to_string()) || load_head(dfs_, dag_, *contract_id).has_value()) {
            return std::unexpected(failure(ContractError::AlreadyExists, "Contract already exists"));
        }
        auto saved = stage_artifacts(record);
        if (!saved.has_value()) {
            return saved;
        }
        auto head = save_head(dfs_, *contract_id, record);
        if (!head.has_value()) {
            return std::unexpected(head.error());
        }
        heads_.insert_or_assign(contract_id->to_string(), std::move(*head));
        return {};
    }

    std::expected<void, ContractFailure> DfsContractStorage::stage(const ContractRecord &record) {
        std::scoped_lock lock(mutex_);
        return stage_artifacts(record);
    }

    std::expected<void, ContractFailure> DfsContractStorage::replace(const ContractRecord &record,
                                                                     std::uint32_t         expected_version,
                                                                     std::string_view      expected_state_hash) {
        std::scoped_lock lock(mutex_);
        auto             contract_id = ActorId::create(record.contract_id);
        if (!contract_id.has_value()) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Contract ID is invalid"));
        }
        auto cached  = heads_.find(contract_id->to_string());
        auto current = cached == heads_.end() ? load_head(dfs_, dag_, *contract_id)
                                              : std::expected<ContractRecord, ContractFailure>(cached->second);
        if (!current.has_value()) {
            return std::unexpected(current.error());
        }
        const auto &current_version = current->versions.at(current->active_version - 1);
        if (current->active_version != expected_version || current_version.revisions.empty()
            || current_version.revisions.back().state_hash != expected_state_hash) {
            return std::unexpected(failure(ContractError::Conflict, "Contract state changed during execution"));
        }
        const auto &next = record.versions.at(record.active_version - 1).revisions.back();
        if (next.checkpoint_revision == next.revision) {
            auto saved = stage_artifacts(record);
            if (!saved.has_value()) {
                return saved;
            }
        }
        auto head = save_head(dfs_, *contract_id, record);
        if (!head.has_value()) {
            return std::unexpected(head.error());
        }
        heads_.insert_or_assign(contract_id->to_string(), std::move(*head));
        return {};
    }

} // namespace ExtraChain::Contracts
