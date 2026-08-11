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

#include <algorithm>
#include <ranges>
#include <type_traits>

namespace ExtraChain::Core {

    template <std::ranges::range Container>
        requires std::is_arithmetic_v<std::ranges::range_value_t<Container>>
    constexpr bool all_zero(const Container& container) {
        return std::ranges::all_of(container, [](const auto value) {
            return value == std::ranges::range_value_t<Container> {};
        });
    }

} // namespace ExtraChain::Core
