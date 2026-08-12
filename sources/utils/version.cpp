/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "utils/version.h"

#include <array>
#include <charconv>

namespace Utils {
    namespace {
        std::array<int, 4> parse_version(std::string_view version) {
            std::array<int, 4> components {};
            for (std::size_t index = 0; index < components.size() && !version.empty(); ++index) {
                const auto separator = version.find('.');
                const auto component = version.substr(0, separator);
                int        value     = 0;
                const auto result = std::from_chars(component.data(), component.data() + component.size(), value);
                if (result.ec == std::errc {}) {
                    components[index] = value;
                }
                if (separator == std::string_view::npos) {
                    break;
                }
                version.remove_prefix(separator + 1);
            }
            return components;
        }
    } // namespace

    VersionCompareResult compare_versions(std::string_view current, std::string_view latest) {
        const auto current_components = parse_version(current);
        const auto latest_components  = parse_version(latest);
        if (latest_components > current_components) {
            return VersionCompareResult::Newer;
        }
        if (latest_components < current_components) {
            return VersionCompareResult::Older;
        }
        return VersionCompareResult::Same;
    }

    bool is_newer_version(std::string_view current, std::string_view latest) {
        return compare_versions(current, latest) == VersionCompareResult::Newer;
    }
} // namespace Utils
