/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include <QString>
#include <boost/describe/class.hpp>

#include "extrachain_global.h"

class ExtraChainNode;

namespace ExtraChain::Contracts {

    struct ToolchainPackage {
        std::string   platform;
        std::string   architecture;
        std::string   archive_format;
        std::string   file_name;
        std::string   file_id;
        std::string   hash;
        std::uint64_t size = 0;
    };
    BOOST_DESCRIBE_STRUCT(ToolchainPackage,
                          (),
                          (platform, architecture, archive_format, file_name, file_id, hash, size))

    struct ToolchainManifest {
        std::uint32_t                 schema           = 1;
        std::uint64_t                 release_sequence = 0;
        std::string                   channel          = "stable";
        std::string                   version;
        std::string                   rust_version;
        std::string                   sdk_version;
        std::string                   contract_abi;
        std::string                   core_min;
        std::string                   core_max;
        std::uint64_t                 created = 0;
        std::vector<ToolchainPackage> packages;
        std::vector<std::string>      revoked_versions;
    };
    BOOST_DESCRIBE_STRUCT(ToolchainManifest,
                          (),
                          (schema,
                           release_sequence,
                           channel,
                           version,
                           rust_version,
                           sdk_version,
                           contract_abi,
                           core_min,
                           core_max,
                           created,
                           packages,
                           revoked_versions))

    enum class ToolchainError {
        Unavailable,
        InvalidManifest,
        InvalidOwner,
        InvalidSignature,
        InvalidHash,
        IncompatibleCore,
        Downgrade,
        Revoked,
        PackageNotFound,
        Unauthorized,
        StorageError
    };

    struct ToolchainFailure {
        ToolchainError error;
        std::string    detail;
    };

    class EXTRACHAIN_EXPORT ToolchainRegistry {
    public:
        explicit ToolchainRegistry(ExtraChainNode* node);

        std::expected<ToolchainManifest, ToolchainFailure> manifest(
            std::uint64_t minimum_release_sequence = 0) const;
        std::expected<std::vector<std::uint8_t>, ToolchainFailure> package(const ToolchainPackage& package) const;
        std::expected<ToolchainPackage, ToolchainFailure>          publish_package(
                     std::string                   platform,
                     std::string                   architecture,
                     std::string                   archive_format,
                     std::string                   version,
                     std::span<const std::uint8_t> content) const;
        std::expected<void, ToolchainFailure> publish_manifest(const ToolchainManifest& manifest) const;

    private:
        ExtraChainNode* node_;
    };

    struct ToolchainInstallation {
        ToolchainManifest manifest;
        QString           path;
    };

    class EXTRACHAIN_EXPORT ToolchainInstaller {
    public:
        ToolchainInstaller(ExtraChainNode* node, QString root_path);

        std::expected<ToolchainInstallation, ToolchainFailure> install_stable(bool allow_first_install);
        std::expected<ToolchainInstallation, ToolchainFailure> current() const;
        std::expected<QString, ToolchainFailure>               build_contract(const QString& source,
                                                                              const QString& project_name,
                                                                              int            timeout_ms = 120000) const;

    private:
        ExtraChainNode* node_;
        QString         root_path_;
    };

} // namespace ExtraChain::Contracts
