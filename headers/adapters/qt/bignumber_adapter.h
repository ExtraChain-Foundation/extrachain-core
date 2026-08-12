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

#include <cstddef>
#include <string>

#include "utils/bignumber_float.h"

inline std::size_t qHash(const BigNumberFloat& key, std::size_t seed = 0) {
    const auto value = key.to_string();
    auto       hash  = seed;
    for (const unsigned char character : value) {
        hash = (hash * 131U) ^ character;
    }
    return hash;
}
