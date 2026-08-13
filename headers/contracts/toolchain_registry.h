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

#include <boost/describe/class.hpp>

#include "extrachain_global.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}

namespace ExtraChain::Contracts {

    enum class ToolchainLanguage {
        Rust,
        AssemblyScript
    };

    [[nodiscard]] EXTRACHAIN_EXPORT std::string_view toolchain_language_name(ToolchainLanguage language);

    [[nodiscard]] EXTRACHAIN_EXPORT std::expected<ToolchainLanguage, std::string> toolchain_language(
        std::string_view value);

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
        std::uint32_t                 schema           = 2;
        std::uint64_t                 release_sequence = 0;
        std::string                   channel          = "stable";
        std::string                   language         = "rust";
        std::string                   version;
        std::string                   compiler_version;
        std::string                   runtime_version;
        std::string                   rust_version;
        std::string                   sdk_version;
        std::string                   components_version;
        std::string                   catalog_version;
        std::string                   template_version;
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
                           language,
                           version,
                           compiler_version,
                           runtime_version,
                           rust_version,
                           sdk_version,
                           components_version,
                           catalog_version,
                           template_version,
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
        explicit ToolchainRegistry(ExtraChain::Core::ExtraChainNode* node);

        std::expected<ToolchainManifest, ToolchainFailure> manifest(
            ToolchainLanguage language,
            std::uint64_t     minimum_release_sequence = 0) const;
        std::expected<ToolchainManifest, ToolchainFailure> manifest(
            std::uint64_t minimum_release_sequence = 0) const;
        std::expected<std::vector<std::uint8_t>, ToolchainFailure> package(const ToolchainPackage& package) const;
        std::expected<ToolchainPackage, ToolchainFailure>          publish_package(
                     ToolchainLanguage             language,
                     std::string                   platform,
                     std::string                   architecture,
                     std::string                   archive_format,
                     std::string                   version,
                     std::span<const std::uint8_t> content) const;
        std::expected<ToolchainPackage, ToolchainFailure> publish_package(
            std::string                   platform,
            std::string                   architecture,
            std::string                   archive_format,
            std::string                   version,
            std::span<const std::uint8_t> content) const;
        std::expected<void, ToolchainFailure> publish_manifest(const ToolchainManifest& manifest) const;

    private:
        ExtraChain::Core::ExtraChainNode* node_;
    };

} // namespace ExtraChain::Contracts
