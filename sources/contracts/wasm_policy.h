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
#include <span>

namespace ExtraChain::Contracts::Internal {

    enum class WasmPolicyResult {
        Accepted,
        Invalid,
        FloatingPoint
    };

    [[nodiscard]] WasmPolicyResult validate_wasm_policy(std::span<const std::uint8_t> module);

} // namespace ExtraChain::Contracts::Internal
