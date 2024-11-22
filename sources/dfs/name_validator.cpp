/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "dfs/name_validator.h"

#include <algorithm>
#include <string>
#include <iterator>

std::expected<void, NameValidator::ValidationError> NameValidator::check_empty(std::string_view name) noexcept {
    if (name.empty()) {
        return std::unexpected(ValidationError { .code = ErrorCode::EmptyName, .position = 0 });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::check_length(std::string_view name) noexcept {
    if (name.length() > MAX_NAME_LENGTH) {
        return std::unexpected(ValidationError { .code = ErrorCode::TooLong, .position = MAX_NAME_LENGTH });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::check_null_byte(
    std::string_view name) noexcept {
    if (auto pos = name.find('\0'); pos != std::string_view::npos) {
        return std::unexpected(ValidationError { .code = ErrorCode::NullByte, .position = pos });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::check_invalid_chars(
    std::string_view name) noexcept {
    for (size_t i = 0; i < name.length(); ++i) {
        if (std::ranges::find(INVALID_CHARS, name[i]) != INVALID_CHARS.end()) {
            return std::unexpected(ValidationError { .code = ErrorCode::InvalidChar, .position = i });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::check_control_chars(
    std::string_view name) noexcept {
    for (size_t i = 0; i < name.length(); ++i) {
        if (static_cast<unsigned char>(name[i]) < 32) {
            return std::unexpected(ValidationError { .code = ErrorCode::ControlChar, .position = i });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::check_boundary_chars(
    std::string_view name) noexcept {
    if (name.back() == '.' || name.back() == ' ' || name.front() == ' ') {
        return std::unexpected(
            ValidationError { .code = ErrorCode::TrailingDotSpace, .position = name.length() - 1 });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::check_consecutive_dots(
    std::string_view name) noexcept {
    for (size_t i = 0; i < name.length() - 1; ++i) {
        if (name[i] == '.' && name[i + 1] == '.') {
            return std::unexpected(ValidationError { .code = ErrorCode::ConsecutiveDots, .position = i });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::check_reserved_name(
    std::string_view name) noexcept {
    std::string upper_name;
    upper_name.reserve(name.size());
    std::transform(name.begin(), name.end(), std::back_inserter(upper_name), [](unsigned char c) {
        return std::toupper(c);
    });

    auto             dot_pos = upper_name.find('.');
    std::string_view base_name(upper_name.data(), dot_pos == std::string::npos ? upper_name.length() : dot_pos);

    for (const auto& reserved : RESERVED_NAMES) {
        if (base_name == reserved) {
            return std::unexpected(ValidationError { .code = ErrorCode::ReservedName, .position = 0 });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError> NameValidator::validate(std::string_view name) noexcept {
    if (auto result = check_empty(name); !result)
        return result;
    if (auto result = check_length(name); !result)
        return result;
    if (auto result = check_null_byte(name); !result)
        return result;
    if (auto result = check_invalid_chars(name); !result)
        return result;
    if (auto result = check_control_chars(name); !result)
        return result;
    if (auto result = check_boundary_chars(name); !result)
        return result;
    if (auto result = check_consecutive_dots(name); !result)
        return result;
    if (auto result = check_reserved_name(name); !result)
        return result;

    return {};
}

std::expected<void, PathValidator::ValidationError> PathValidator::validate(std::string_view path) noexcept {
    if (path.empty()) {
        return std::unexpected(
            ValidationError { .code = ErrorCode::EmptyPath, .position = 0, .name_error = std::nullopt });
    }

    if (path.length() > MAX_PATH_LENGTH) {
        return std::unexpected(ValidationError { .code       = ErrorCode::TooLong,
                                                 .position   = MAX_PATH_LENGTH,
                                                 .name_error = std::nullopt });
    }

    // Handle Windows drive letter (e.g., "C:")
    const bool has_drive =
        path.length() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';

    if (has_drive && (path.length() == 2 || (path[2] != '/' && path[2] != '\\'))) {
        return std::unexpected(
            ValidationError { .code = ErrorCode::InvalidDriveLetter, .position = 2, .name_error = std::nullopt });
    }

    const size_t start_pos       = has_drive ? 2 : 0;
    size_t       component_start = start_pos;

    for (size_t i = start_pos; i <= path.length(); ++i) {
        const bool is_separator = i < path.length() && (path[i] == '/' || path[i] == '\\');
        const bool is_end       = i == path.length();

        if (!is_separator && !is_end) {
            continue;
        }

        const size_t component_length = i - component_start;
        if (component_length == 0 && i != start_pos) {
            return std::unexpected(
                ValidationError { .code = ErrorCode::EmptyComponent, .position = i, .name_error = std::nullopt });
        }

        if (component_length > 0) {
            if (auto result = NameValidator::validate(path.substr(component_start, component_length)); !result) {
                return std::unexpected(ValidationError { .code       = ErrorCode::InvalidName,
                                                         .position   = component_start,
                                                         .name_error = result.error() });
            }
        }

        component_start = i + 1;
    }

    return {};
}
