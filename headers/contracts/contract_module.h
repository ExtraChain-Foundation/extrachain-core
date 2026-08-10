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
#include <vector>

#include "extrachain_global.h"

namespace ExtraChain::Contracts {

    inline constexpr std::string_view ContractLanguageSection = "extrachain.language";
    inline constexpr std::string_view PythonRuntimeSection    = "extrachain.python.runtime";
    inline constexpr std::string_view PythonSdkSection        = "extrachain.python.sdk";
    inline constexpr std::string_view PythonBytecodeSection   = "extrachain.python.bytecode";
    inline constexpr std::string_view PythonSourceSection     = "extrachain.python.source";

    [[nodiscard]] EXTRACHAIN_EXPORT std::expected<std::string, std::string> module_language(
        std::span<const std::uint8_t> module);

    [[nodiscard]] EXTRACHAIN_EXPORT std::expected<std::vector<std::uint8_t>, std::string> module_custom_section(
        std::span<const std::uint8_t> module,
        std::string_view              name);

} // namespace ExtraChain::Contracts
