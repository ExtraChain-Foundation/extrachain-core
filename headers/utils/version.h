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

#include <string_view>

#include "extrachain_global.h"

namespace Utils {
    enum class VersionCompareResult {
        Newer,
        Older,
        Same
    };

    EXTRACHAIN_EXPORT VersionCompareResult compare_versions(std::string_view current, std::string_view latest);
    EXTRACHAIN_EXPORT bool                 is_newer_version(std::string_view current, std::string_view latest);
} // namespace Utils
