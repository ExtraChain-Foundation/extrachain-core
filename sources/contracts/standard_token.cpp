/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "contracts/standard_token.h"

#include <array>
#include <span>

#include "contracts/contract_hash.h"
#include "contracts/embedded_contracts.h"

namespace ExtraChain::Contracts {
    namespace {
        std::span<const std::uint8_t> embedded_module(std::string_view kind, ToolchainLanguage language) {
            if (kind == FungibleTokenKind && language == ToolchainLanguage::Rust) {
                return Embedded::fungible_token_rust;
            }
            if (kind == FungibleTokenKind && language == ToolchainLanguage::AssemblyScript) {
                return Embedded::fungible_token_assemblyscript;
            }
            if (kind == NonFungibleTokenKind && language == ToolchainLanguage::Rust) {
                return Embedded::non_fungible_token_rust;
            }
            if (kind == NonFungibleTokenKind && language == ToolchainLanguage::AssemblyScript) {
                return Embedded::non_fungible_token_assemblyscript;
            }
            return {};
        }
    } // namespace

    bool is_system_token_kind(std::string_view kind) {
        return kind == FungibleTokenKind || kind == NonFungibleTokenKind;
    }

    std::expected<std::vector<std::uint8_t>, std::string> standard_token_module(std::string_view  kind,
                                                                                ToolchainLanguage language) {
        const auto module = embedded_module(kind, language);
        if (module.empty()) {
            return std::unexpected("Token kind is not supported");
        }
        return std::vector<std::uint8_t>(module.begin(), module.end());
    }

    bool is_standard_token_module(std::string_view kind, std::string_view module_hash) {
        if (kind == FungibleTokenKind
            && (module_hash == "c23f13167d23eb39f0d6def51cb80f56f3ef1dc1af8fd74669277bee48669103"
                || module_hash == "afe321f3e5ff054243bbafd2215fadabb6a0668aef0b7899ea7cd0c11561a46b")) {
            return true;
        }
        for (const auto language : std::array { ToolchainLanguage::Rust, ToolchainLanguage::AssemblyScript }) {
            const auto module = standard_token_module(kind, language);
            if (module.has_value() && content_hash(*module) == module_hash) {
                return true;
            }
        }
        return false;
    }

} // namespace ExtraChain::Contracts
