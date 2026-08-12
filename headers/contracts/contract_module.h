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
#include <string_view>

#include "extrachain_global.h"

namespace ExtraChain::Contracts {

    inline constexpr std::string_view ContractLanguageSection = "extrachain.language";

    [[nodiscard]] EXTRACHAIN_EXPORT std::expected<std::string, std::string> module_language(
        std::span<const std::uint8_t> module);

} // namespace ExtraChain::Contracts
