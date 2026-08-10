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

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/toolchain_registry.h"
#include "extrachain_global.h"

namespace ExtraChain::Contracts {

    inline constexpr std::string_view FungibleTokenKind    = "fungible-token";
    inline constexpr std::string_view NonFungibleTokenKind = "non-fungible-token";

    [[nodiscard]] EXTRACHAIN_EXPORT bool is_system_token_kind(std::string_view kind);

    [[nodiscard]] EXTRACHAIN_EXPORT std::expected<std::vector<std::uint8_t>, std::string> standard_token_module(
        std::string_view  kind,
        ToolchainLanguage language);

    [[nodiscard]] EXTRACHAIN_EXPORT bool is_standard_token_module(std::string_view kind,
                                                                  std::string_view module_hash);

} // namespace ExtraChain::Contracts
