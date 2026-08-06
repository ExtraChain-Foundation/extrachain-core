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

#include "contracts/contract_types.h"
#include "extrachain_global.h"

namespace ExtraChain::Contracts {

    class EXTRACHAIN_EXPORT WasmRuntime {
    public:
        explicit WasmRuntime(ExecutionLimits limits = {});
        ~WasmRuntime();

        WasmRuntime(const WasmRuntime &)            = delete;
        WasmRuntime &operator=(const WasmRuntime &) = delete;
        WasmRuntime(WasmRuntime &&)                 = delete;
        WasmRuntime &operator=(WasmRuntime &&)      = delete;

        [[nodiscard]] bool available() const;

        [[nodiscard]] std::expected<ExecutionResult, ExecutionFailure> invoke(
            std::span<const std::uint8_t> module,
            std::span<const std::uint8_t> input) const;

    private:
        ExecutionLimits limits_;
        bool            available_ = false;
    };

} // namespace ExtraChain::Contracts
