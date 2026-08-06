/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "contracts/toolchain_registry.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include "chain/actor_index.h"
#include "dfs/dfs_controller.h"
#include "dfs/dfs_utils.h"
#include "extrachain_version.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "utils/exc_utils.h"

namespace ExtraChain::Contracts {
    namespace {
        constexpr std::uint64_t MaxToolchainPackageSize = 2ULL * 1024 * 1024 * 1024;

        ToolchainFailure failure(ToolchainError error, std::string detail) {
            return { error, std::move(detail) };
        }

        std::string content_hash(std::span<const std::uint8_t> content) {
            return Utils::calculate_hash(std::string(reinterpret_cast<const char*>(content.data()),
                                                     content.size()),
                                         Utils::HashAlgorithm::Blake3);
        }

        std::optional<std::array<std::uint64_t, 3>> version_parts(std::string_view value) {
            std::array<std::uint64_t, 3> parts {};
            std::size_t                  start = 0;
            if (value.empty()) {
                return std::nullopt;
            }
            for (std::size_t index = 0; index < parts.size(); ++index) {
                const auto end = value.find('.', start);
                if ((index < parts.size() - 1 && end == std::string_view::npos)
                    || (index == parts.size() - 1 && end != std::string_view::npos)) {
                    return std::nullopt;
                }
                const auto part =
                    value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
                if (part.empty() || !std::ranges::all_of(part, [](char character) {
                        return character >= '0' && character <= '9';
                    })) {
                    return std::nullopt;
                }
                try {
                    parts[index] = std::stoull(std::string(part));
                } catch (...) {
                    return std::nullopt;
                }
                if (end != std::string_view::npos) {
                    start = end + 1;
                }
            }
            return parts;
        }

        bool compatible(std::string_view current, std::string_view minimum, std::string_view maximum) {
            const auto current_parts = version_parts(current);
            const auto minimum_parts = version_parts(minimum);
            const auto maximum_parts = version_parts(maximum);
            return current_parts.has_value() && minimum_parts.has_value() && maximum_parts.has_value()
                   && *current_parts >= *minimum_parts && *current_parts <= *maximum_parts;
        }

        bool supported_archive(std::string_view value) {
            return value == "tar" || value == "tar.gz" || value == "tar.xz" || value == "tar.zst";
        }

        bool safe_identifier(std::string_view value) {
            return !value.empty() && value.size() <= 32 && std::ranges::all_of(value, [](char character) {
                return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9')
                       || character == '-' || character == '_';
            });
        }

        bool safe_file_name(std::string_view value) {
            return !value.empty() && value.size() <= 200 && value.find('/') == std::string_view::npos
                   && value.find('\\') == std::string_view::npos && value != "." && value != "..";
        }

        bool valid_hash(std::string_view value) {
            return value.size() == 64 && std::ranges::all_of(value, [](char character) {
                       return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
                   });
        }

        bool valid_package(const ToolchainPackage& package) {
            return safe_identifier(package.platform) && safe_identifier(package.architecture)
                   && supported_archive(package.archive_format) && safe_file_name(package.file_name)
                   && !package.file_id.empty() && valid_hash(package.hash) && package.size > 0
                   && package.size <= MaxToolchainPackageSize;
        }

        bool verify_signed_row(ExtraChainNode* node, Dfs::DirRow row, const ActorId& network_id) {
            if (row.owner_id != network_id || row.actor_id != network_id) {
                return false;
            }
            const auto actor = node->actor_index()->read_actor(network_id, ActorGetType::NoRequest);
            if (!actor.has_value()) {
                return false;
            }
            const auto verified = actor->key().verify(row.calculate_hash(network_id), row.sign);
            return verified.has_value() && *verified;
        }

        bool verify_toolchain_row(ExtraChainNode* node, Dfs::DirRow row, const ActorId& network_id) {
            return row.state == Dfs::FileState::Ready && !row.encryption
                   && row.folder == Dfs::Basic::TEMPLATE_CONTRACT_TOOLCHAIN
                   && verify_signed_row(node, std::move(row), network_id);
        }

        bool verify_chain(ExtraChainNode*                 node,
                          const std::vector<Dfs::DirRow>& rows,
                          const Dfs::DirRow&              leaf,
                          const ActorId&                  network_id) {
            std::unordered_map<std::string, Dfs::DirRow> by_id;
            by_id.reserve(rows.size());
            for (const auto& row : rows) {
                by_id.emplace(row.file_id, row);
            }
            std::unordered_set<std::string> visited;
            auto                            current = leaf;
            while (true) {
                if (!visited.emplace(current.file_id).second || !verify_signed_row(node, current, network_id)) {
                    return false;
                }
                if (!current.prev_file_id.has_value() || current.prev_file_id->empty()) {
                    return true;
                }
                const auto previous = by_id.find(*current.prev_file_id);
                if (previous == by_id.end()) {
                    return false;
                }
                current = previous->second;
            }
        }

        std::expected<std::vector<std::uint8_t>, ToolchainFailure> read_content(ExtraChainNode*    node,
                                                                                const ActorId&     network_id,
                                                                                const Dfs::DirRow& row) {
            auto content = Dfs::Tables::DirsFile::ActorSpace::get_file_content(network_id, row.file_id);
            if (!content.has_value()) {
                node->dfs()->request_file(network_id, row.file_id);
                return std::unexpected(
                    failure(ToolchainError::Unavailable, "Toolchain data is not available locally"));
            }
            if (content->size() != row.size || content_hash(*content) != row.hash) {
                return std::unexpected(
                    failure(ToolchainError::InvalidHash, "Toolchain DFS content does not match its metadata"));
            }
            return *content;
        }
    } // namespace

    ToolchainRegistry::ToolchainRegistry(ExtraChainNode* node)
        : node_(node) {
    }

    std::expected<ToolchainManifest, ToolchainFailure> ToolchainRegistry::manifest(
        std::uint64_t minimum_release_sequence) const {
        if (node_ == nullptr || node_->dfs() == nullptr || node_->actor_index() == nullptr) {
            return std::unexpected(failure(ToolchainError::Unavailable, "ExtraChain DFS is not ready"));
        }
        const auto network_id = node_->network_id();
        if (network_id.is_zero()) {
            return std::unexpected(failure(ToolchainError::Unavailable, "ExtraChain network ID is not ready"));
        }
        auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(node_->dfs()->get_db_instance(), network_id);
        if (!rows.has_value()) {
            return std::unexpected(failure(ToolchainError::Unavailable, "Toolchain manifest is not known"));
        }
        std::vector<Dfs::DirRow> manifests;
        std::ranges::copy_if(*rows, std::back_inserter(manifests), [](const Dfs::DirRow& row) {
            return row.folder == Dfs::Basic::TEMPLATE_CONTRACT_TOOLCHAIN
                   && row.name.starts_with("contract-toolchain-manifest-") && row.name.ends_with(".json")
                   && row.state == Dfs::FileState::Ready;
        });
        std::ranges::sort(manifests, std::greater {}, &Dfs::DirRow::name);
        std::optional<ToolchainFailure> unavailable;
        for (auto row : manifests) {
            if (!verify_toolchain_row(node_, row, network_id) || !verify_chain(node_, *rows, row, network_id)) {
                continue;
            }
            auto content = read_content(node_, network_id, row);
            if (!content.has_value()) {
                if (content.error().error == ToolchainError::Unavailable) {
                    unavailable = content.error();
                }
                continue;
            }
            const auto parsed = Json::deserialize<ToolchainManifest>(
                std::string(reinterpret_cast<const char*>(content->data()), content->size()));
            if (!parsed.has_value() || parsed->schema != 1 || parsed->release_sequence == 0
                || parsed->channel != "stable" || parsed->version.empty() || parsed->rust_version.empty()
                || parsed->sdk_version.empty() || parsed->contract_abi != std::to_string(ContractAbiVersion)
                || parsed->created == 0 || parsed->packages.empty() || parsed->core_min.empty()
                || parsed->core_max.empty() || !version_parts(parsed->version).has_value()
                || !version_parts(parsed->core_min).has_value() || !version_parts(parsed->core_max).has_value()
                || *version_parts(parsed->core_min) > *version_parts(parsed->core_max)
                || std::ranges::any_of(parsed->packages, [](const ToolchainPackage& package) {
                       return !valid_package(package);
                   })) {
                continue;
            }
            if (parsed->release_sequence < minimum_release_sequence) {
                return std::unexpected(
                    failure(ToolchainError::Downgrade, "Toolchain release is older than the accepted release"));
            }
            if (!compatible(extrachain_version, parsed->core_min, parsed->core_max)) {
                return std::unexpected(failure(ToolchainError::IncompatibleCore,
                                               "Toolchain release is not compatible with this Core"));
            }
            if (std::ranges::find(parsed->revoked_versions, parsed->version) != parsed->revoked_versions.end()) {
                return std::unexpected(failure(ToolchainError::Revoked, "Toolchain release is revoked"));
            }
            return *parsed;
        }
        if (unavailable.has_value()) {
            return std::unexpected(*unavailable);
        }
        return std::unexpected(failure(ToolchainError::InvalidManifest, "No valid toolchain manifest was found"));
    }

    std::expected<std::vector<std::uint8_t>, ToolchainFailure> ToolchainRegistry::package(
        const ToolchainPackage& requested) const {
        if (node_ == nullptr || node_->dfs() == nullptr) {
            return std::unexpected(failure(ToolchainError::Unavailable, "ExtraChain DFS is not ready"));
        }
        const auto network_id = node_->network_id();
        auto       row        = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node_->dfs()->get_db_instance(),
                                                                  network_id,
                                                                  requested.file_id,
                                                                  "file_id");
        const auto rows =
            Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(node_->dfs()->get_db_instance(), network_id);
        if (!row.has_value() || !rows.has_value() || row->name != requested.file_name
            || !verify_toolchain_row(node_, *row, network_id) || !verify_chain(node_, *rows, *row, network_id)) {
            return std::unexpected(failure(ToolchainError::PackageNotFound,
                                           "Toolchain package is not signed by the network address"));
        }
        auto content = read_content(node_, network_id, *row);
        if (!content.has_value()) {
            return std::unexpected(content.error());
        }
        if (requested.size != content->size() || requested.hash != content_hash(*content)) {
            return std::unexpected(
                failure(ToolchainError::InvalidHash, "Toolchain package does not match the manifest"));
        }
        return *content;
    }

    std::expected<ToolchainPackage, ToolchainFailure> ToolchainRegistry::publish_package(
        std::string                   platform,
        std::string                   architecture,
        std::string                   archive_format,
        std::string                   version,
        std::span<const std::uint8_t> content) const {
        if (node_ == nullptr || node_->dfs() == nullptr || content.empty()
            || content.size() > MaxToolchainPackageSize || !safe_identifier(platform)
            || !safe_identifier(architecture) || !supported_archive(archive_format)
            || !version_parts(version).has_value()) {
            return std::unexpected(failure(ToolchainError::StorageError, "Toolchain package is empty"));
        }
        const auto network_id = node_->network_id();
        const auto wallet     = node_->account_controller()->current_wallet();
        if (network_id.is_zero() || wallet.empty() || wallet.id() != network_id) {
            return std::unexpected(failure(ToolchainError::Unauthorized,
                                           "Only the ExtraChain network address can publish toolchains"));
        }
        const auto hash   = content_hash(content);
        const auto name   = fmt::format("contract-toolchain-{}-{}-{}-{}.{}",
                                      version,
                                      platform,
                                      architecture,
                                      hash.substr(0, 12),
                                      archive_format);
        auto       stored = node_->dfs()->store_data_as_file(network_id,
                                                       network_id,
                                                             { content.begin(), content.end() },
                                                       Dfs::Basic::TEMPLATE_CONTRACT_TOOLCHAIN,
                                                       name,
                                                       Dfs::DataSecurity::Public);
        if (!stored.has_value()) {
            return std::unexpected(
                failure(ToolchainError::StorageError, "Cannot publish the toolchain package to DFS"));
        }
        return ToolchainPackage {
            .platform       = std::move(platform),
            .architecture   = std::move(architecture),
            .archive_format = std::move(archive_format),
            .file_name      = name,
            .file_id        = stored->file_id,
            .hash           = hash,
            .size           = static_cast<std::uint64_t>(content.size()),
        };
    }

    std::expected<void, ToolchainFailure> ToolchainRegistry::publish_manifest(
        const ToolchainManifest& value) const {
        if (node_ == nullptr || node_->dfs() == nullptr || value.schema != 1 || value.release_sequence == 0
            || value.channel != "stable" || value.version.empty() || value.rust_version.empty()
            || value.sdk_version.empty() || value.contract_abi != std::to_string(ContractAbiVersion)
            || value.created == 0 || value.packages.empty() || !version_parts(value.version).has_value()
            || !version_parts(value.core_min).has_value() || !version_parts(value.core_max).has_value()
            || *version_parts(value.core_min) > *version_parts(value.core_max)
            || std::ranges::any_of(value.packages, [](const ToolchainPackage& package) {
                   return !valid_package(package);
               })) {
            return std::unexpected(failure(ToolchainError::InvalidManifest, "Toolchain manifest is incomplete"));
        }
        const auto network_id = node_->network_id();
        const auto wallet     = node_->account_controller()->current_wallet();
        if (network_id.is_zero() || wallet.empty() || wallet.id() != network_id) {
            return std::unexpected(failure(ToolchainError::Unauthorized,
                                           "Only the ExtraChain network address can publish toolchains"));
        }
        const auto current = manifest();
        if (current.has_value() && value.release_sequence <= current->release_sequence) {
            return std::unexpected(failure(ToolchainError::Downgrade, "Toolchain release sequence must increase"));
        }
        for (const auto& artifact : value.packages) {
            auto verified = package(artifact);
            if (!verified.has_value()) {
                return std::unexpected(verified.error());
            }
        }
        const auto json   = Json::serialize(value);
        const auto name   = fmt::format("contract-toolchain-manifest-r{:020}.json", value.release_sequence);
        auto       stored = node_->dfs()->store_data_as_file(network_id,
                                                       network_id,
                                                             { json.begin(), json.end() },
                                                       Dfs::Basic::TEMPLATE_CONTRACT_TOOLCHAIN,
                                                       name,
                                                       Dfs::DataSecurity::Public);
        if (!stored.has_value()) {
            return std::unexpected(
                failure(ToolchainError::StorageError, "Cannot publish the toolchain manifest to DFS"));
        }
        return {};
    }

} // namespace ExtraChain::Contracts
