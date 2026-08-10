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

#include <QFile>

#include "contracts/contract_hash.h"

namespace ExtraChain::Contracts {
    namespace {
        std::string resource_name(std::string_view kind, ToolchainLanguage language) {
            const auto suffix = language == ToolchainLanguage::AssemblyScript ? "assemblyscript" : "rust";
            if (kind == FungibleTokenKind) {
                return ":/contracts/fungible_token_" + std::string(suffix) + ".wasm";
            }
            if (kind == NonFungibleTokenKind) {
                return ":/contracts/non_fungible_token_" + std::string(suffix) + ".wasm";
            }
            return {};
        }
    } // namespace

    bool is_system_token_kind(std::string_view kind) {
        return kind == FungibleTokenKind || kind == NonFungibleTokenKind;
    }

    std::expected<std::vector<std::uint8_t>, std::string> standard_token_module(std::string_view  kind,
                                                                                ToolchainLanguage language) {
        const auto name = resource_name(kind, language);
        if (name.empty()) {
            return std::unexpected("Token kind is not supported");
        }
        QFile file(QString::fromStdString(name));
        if (!file.open(QIODevice::ReadOnly)) {
            return std::unexpected("Standard token module is not available");
        }
        const auto bytes = file.readAll();
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }

    bool is_standard_token_module(std::string_view kind, std::string_view module_hash) {
        for (const auto language : std::array { ToolchainLanguage::Rust, ToolchainLanguage::AssemblyScript }) {
            const auto module = standard_token_module(kind, language);
            if (module.has_value() && content_hash(*module) == module_hash) {
                return true;
            }
        }
        return false;
    }

} // namespace ExtraChain::Contracts
