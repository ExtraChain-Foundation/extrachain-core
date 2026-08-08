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

#include "contracts/contract_types.h"

namespace ExtraChain::Contracts::Codec {

    [[nodiscard]] std::vector<std::uint8_t> encode_request(const ExecutionContext       &context,
                                                           std::string_view              method,
                                                           std::span<const std::uint8_t> arguments,
                                                           std::span<const std::uint8_t> state,
                                                           const VerifiedInputs         &verified = {});

    [[nodiscard]] std::vector<std::uint8_t> encode_string(std::string_view value);

    [[nodiscard]] std::expected<std::vector<std::uint8_t>, ContractFailure> encode_json(std::string_view json);

    [[nodiscard]] std::expected<std::string, ContractFailure> decode_json(std::span<const std::uint8_t> value);

    [[nodiscard]] std::expected<ContractOutput, ContractFailure> decode_response(
        std::span<const std::uint8_t> response);

    [[nodiscard]] std::vector<std::uint8_t> encode_effects(std::span<const ContractEffect> effects);

    [[nodiscard]] std::expected<std::vector<ContractEffect>, ContractFailure> decode_effects(
        std::span<const std::uint8_t> encoded);

    [[nodiscard]] std::string effect_hash(std::span<const ContractEffect> effects);

} // namespace ExtraChain::Contracts::Codec
