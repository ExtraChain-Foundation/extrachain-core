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

#include "chain/dag.h"
#include "contracts/contract_transaction.h"
#include <fmt/format.h>
#include <msgpack.hpp>

#include "dfs/dfs_controller.h"
#include "dfs/dfs_utils.h"
#include "utils/exc_utils.h"

namespace ExtraChain::Contracts {
    namespace {

        struct RevisionReference {
            std::uint64_t revision;
            std::uint64_t block;
            std::string   previous_hash;
            std::string   state_hash;
            std::string   transaction_hash;
            std::string   author_id;
            std::string   state_file_id;

            MSGPACK_DEFINE(revision, block, previous_hash, state_hash, transaction_hash, author_id, state_file_id)
        };

        struct VersionReference {
            std::uint32_t                  version;
            std::string                    module_hash;
            std::string                    previous_module_hash;
            std::string                    module_file_id;
            std::vector<RevisionReference> revisions;

            MSGPACK_DEFINE(version, module_hash, previous_module_hash, module_file_id, revisions)
        };

        struct ContractManifest {
            std::uint32_t                 schema = 1;
            std::string                   contract_id;
            std::string                   owner_id;
            std::string                   kind;
            std::uint32_t                 active_version;
            std::vector<VersionReference> versions;

            MSGPACK_DEFINE(schema, contract_id, owner_id, kind, active_version, versions)
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

        bool validate_manifest(DfsController          *dfs,
                               Dag                    *dag,
                               const ActorId          &contract_id,
                               const ContractManifest &manifest) {
            if (manifest.schema != 1 || manifest.contract_id != contract_id.to_string()
                || manifest.owner_id.empty() || manifest.kind.empty() || manifest.active_version == 0
                || manifest.active_version != manifest.versions.size()) {
                return false;
            }

            std::string   previous_module_hash;
            std::string   previous_version_state_hash;
            std::uint64_t previous_version_revision = 0;
            for (std::size_t version_index = 0; version_index < manifest.versions.size(); ++version_index) {
                const auto &version = manifest.versions[version_index];
                if (version.version != version_index + 1 || version.previous_module_hash != previous_module_hash
                    || version.revisions.size() != 1) {
                    return false;
                }
                const auto &revision = version.revisions.front();
                if (revision.revision <= previous_version_revision || revision.transaction_hash.empty()
                    || revision.author_id.empty()) {
                    return false;
                }
                if (version.version == manifest.active_version) {
                    auto module = read_file(dfs, contract_id, version.module_file_id);
                    if (!module.has_value() || content_hash(*module) != version.module_hash) {
                        return false;
                    }
                    auto state = read_file(dfs, contract_id, revision.state_file_id);
                    if (!state.has_value() || content_hash(*state) != revision.state_hash) {
                        return false;
                    }
                }
                auto transaction = find_transaction(dag, revision.block, revision.transaction_hash);
                if (!transaction.has_value() || transaction->receiver() != contract_id
                    || transaction->sender().to_string() != revision.author_id
                    || !transaction->meta().has_value()) {
                    return false;
                }
                auto metadata = Json::deserialize<ContractTransactionData>(*transaction->meta());
                if (!metadata.has_value() || metadata->schema != 1 || metadata->kind != manifest.kind
                    || metadata->module_hash != version.module_hash
                    || metadata->previous_state_hash != revision.previous_hash
                    || metadata->state_hash != revision.state_hash || metadata->version != version.version
                    || metadata->revision != revision.revision) {
                    return false;
                }

                auto expected_type = TransactionType::ContractCall;
                if (version_index == 0 && revision.revision == 1) {
                    expected_type = TransactionType::ContractDeploy;
                    if (revision.author_id != manifest.owner_id || metadata->method != "init") {
                        return false;
                    }
                } else if (version_index > 0 && revision.revision == previous_version_revision + 1) {
                    expected_type = TransactionType::ContractUpgrade;
                    if (revision.author_id != manifest.owner_id || metadata->method != "migrate"
                        || revision.previous_hash != previous_version_state_hash) {
                        return false;
                    }
                }
                if (transaction->type() != expected_type) {
                    return false;
                }

                previous_module_hash        = version.module_hash;
                previous_version_state_hash = revision.state_hash;
                previous_version_revision   = revision.revision;
            }
            return true;
        }

        std::expected<ContractManifest, ContractFailure> load_manifest(DfsController *dfs,
                                                                       Dag           *dag,
                                                                       const ActorId &contract_id) {
            auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(dfs->get_db_instance(), contract_id);
            if (!rows.has_value()) {
                return std::unexpected(failure(ContractError::NotFound, "Contract manifest does not exist"));
            }
            std::vector<Dfs::DirRow> manifests;
            std::ranges::copy_if(*rows, std::back_inserter(manifests), [](const Dfs::DirRow &row) {
                return row.folder == Dfs::Basic::TEMPLATE_CONTRACTS && row.name.starts_with("contract-manifest-")
                       && row.state == Dfs::FileState::Ready;
            });
            if (manifests.empty()) {
                return std::unexpected(failure(ContractError::NotFound, "Contract manifest does not exist"));
            }
            std::ranges::sort(manifests, std::greater {}, &Dfs::DirRow::name);
            for (const auto &row : manifests) {
                auto content = read_file(dfs, contract_id, row.file_id);
                if (!content.has_value()) {
                    continue;
                }
                try {
                    auto object =
                        msgpack::unpack(reinterpret_cast<const char *>(content->data()), content->size());
                    auto manifest = object.get().as<ContractManifest>();
                    if (!validate_manifest(dfs, dag, contract_id, manifest)) {
                        continue;
                    }
                    return manifest;
                } catch (const std::exception &) {
                    continue;
                }
            }
            return std::unexpected(
                failure(ContractError::StorageError, "No contract manifest matches the approved chain"));
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
        auto manifest = load_manifest(dfs_, dag_, *contract_id);
        if (!manifest.has_value()) {
            return std::unexpected(manifest.error());
        }
        if (manifest->contract_id != contract_id_value || manifest->versions.empty()
            || manifest->active_version == 0 || manifest->active_version > manifest->versions.size()) {
            return std::unexpected(failure(ContractError::StorageError, "Contract manifest is invalid"));
        }

        ContractRecord record {
            .contract_id    = manifest->contract_id,
            .owner_id       = manifest->owner_id,
            .kind           = manifest->kind,
            .active_version = manifest->active_version,
        };
        record.versions.reserve(manifest->versions.size());
        for (const auto &version_reference : manifest->versions) {
            std::vector<std::uint8_t> module;
            if (version_reference.version == manifest->active_version) {
                auto loaded_module = read_file(dfs_, *contract_id, version_reference.module_file_id);
                if (!loaded_module.has_value()) {
                    return std::unexpected(loaded_module.error());
                }
                module = std::move(*loaded_module);
            }
            ContractVersion version {
                .version              = version_reference.version,
                .module_hash          = version_reference.module_hash,
                .previous_module_hash = version_reference.previous_module_hash,
                .module_storage_id    = version_reference.module_file_id,
                .module               = std::move(module),
            };
            version.revisions.reserve(version_reference.revisions.size());
            for (std::size_t revision_index = 0; revision_index < version_reference.revisions.size();
                 ++revision_index) {
                const auto               &revision_reference = version_reference.revisions[revision_index];
                std::vector<std::uint8_t> state;
                if (version_reference.version == manifest->active_version
                    && revision_index + 1 == version_reference.revisions.size()) {
                    auto loaded_state = read_file(dfs_, *contract_id, revision_reference.state_file_id);
                    if (!loaded_state.has_value()) {
                        return std::unexpected(loaded_state.error());
                    }
                    state = std::move(*loaded_state);
                }
                version.revisions.push_back(StateRevision {
                    .revision         = revision_reference.revision,
                    .block            = revision_reference.block,
                    .previous_hash    = revision_reference.previous_hash,
                    .state_hash       = revision_reference.state_hash,
                    .transaction_hash = revision_reference.transaction_hash,
                    .author_id        = revision_reference.author_id,
                    .storage_id       = revision_reference.state_file_id,
                    .state            = std::move(state),
                });
            }
            if (version.revisions.empty()) {
                return std::unexpected(failure(ContractError::StorageError, "Contract version has no state"));
            }
            record.versions.push_back(std::move(version));
        }
        return record;
    }

    std::expected<void, ContractFailure> DfsContractStorage::save(const ContractRecord &record,
                                                                  bool                  write_manifest) const {
        auto contract_id = ActorId::create(record.contract_id);
        auto owner_id    = ActorId::create(record.owner_id);
        if (!contract_id.has_value() || !owner_id.has_value()) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Contract owner or ID is invalid"));
        }

        ContractManifest manifest {
            .schema         = 1,
            .contract_id    = record.contract_id,
            .owner_id       = record.owner_id,
            .kind           = record.kind,
            .active_version = record.active_version,
        };
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
            VersionReference version_reference {
                .version              = version.version,
                .module_hash          = version.module_hash,
                .previous_module_hash = version.previous_module_hash,
                .module_file_id       = std::move(module_file_id),
            };
            if (version.revisions.empty()) {
                return std::unexpected(failure(ContractError::StorageError, "Contract version has no state"));
            }
            for (const auto &revision : std::span(version.revisions).last(1)) {
                std::string state_file_id = revision.storage_id;
                if (state_file_id.empty()) {
                    auto state_name      = fmt::format("contract-state-v{:06}-r{:012}-{}.msgpack",
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
                    state_file_id = state_row->file_id;
                }
                if (state_file_id.empty()) {
                    return std::unexpected(failure(ContractError::StorageError, "Contract state file is missing"));
                }
                version_reference.revisions.push_back(RevisionReference {
                    .revision         = revision.revision,
                    .block            = revision.block,
                    .previous_hash    = revision.previous_hash,
                    .state_hash       = revision.state_hash,
                    .transaction_hash = revision.transaction_hash,
                    .author_id        = revision.author_id,
                    .state_file_id    = std::move(state_file_id),
                });
            }
            manifest.versions.push_back(std::move(version_reference));
        }

        if (!write_manifest) {
            return {};
        }

        msgpack::sbuffer buffer;
        msgpack::pack(buffer, manifest);
        auto  manifest_name   = fmt::format("contract-manifest-v{:06}-r{:012}-{}.msgpack",
                                         record.active_version,
                                         record.versions.back().revisions.back().revision,
                                         prefix(record.versions.back().revisions.back().state_hash));
        auto *begin           = reinterpret_cast<const std::uint8_t *>(buffer.data());
        auto  manifest_author = ActorId::create(record.versions.back().revisions.back().author_id);
        if (!manifest_author.has_value()) {
            return std::unexpected(failure(ContractError::InvalidOwner, "Contract manifest author is invalid"));
        }
        auto manifest_row =
            store_file(dfs_, *contract_id, *manifest_author, std::span(begin, buffer.size()), manifest_name);
        if (!manifest_row.has_value()) {
            return std::unexpected(manifest_row.error());
        }
        return {};
    }

    std::expected<void, ContractFailure> DfsContractStorage::create(const ContractRecord &record) {
        std::scoped_lock lock(mutex_);
        auto             contract_id = ActorId::create(record.contract_id);
        if (!contract_id.has_value()) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Contract ID is invalid"));
        }
        auto existing = load_manifest(dfs_, dag_, *contract_id);
        if (existing.has_value()) {
            return std::unexpected(failure(ContractError::AlreadyExists, "Contract already exists"));
        }
        return save(record, true);
    }

    std::expected<void, ContractFailure> DfsContractStorage::stage(const ContractRecord &record) {
        std::scoped_lock lock(mutex_);
        return save(record, false);
    }

    std::expected<void, ContractFailure> DfsContractStorage::replace(const ContractRecord &record,
                                                                     std::uint32_t         expected_version,
                                                                     std::string_view      expected_state_hash) {
        std::scoped_lock lock(mutex_);
        auto             contract_id = ActorId::create(record.contract_id);
        if (!contract_id.has_value()) {
            return std::unexpected(failure(ContractError::InvalidArguments, "Contract ID is invalid"));
        }
        auto manifest = load_manifest(dfs_, dag_, *contract_id);
        if (!manifest.has_value()) {
            return std::unexpected(manifest.error());
        }
        if (manifest->active_version != expected_version || manifest->versions.empty()
            || manifest->versions.back().revisions.empty()
            || manifest->versions.back().revisions.back().state_hash != expected_state_hash) {
            return std::unexpected(failure(ContractError::Conflict, "Contract state changed during execution"));
        }
        return save(record, true);
    }

} // namespace ExtraChain::Contracts
