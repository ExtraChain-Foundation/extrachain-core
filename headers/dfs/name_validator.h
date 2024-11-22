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

#pragma once

#include <string_view>
#include <array>
#include <expected>
#include <optional>

class NameValidator {
public:
    enum class ErrorCode {
        EmptyName,
        TooLong,
        InvalidChar,
        ControlChar,
        ReservedName,
        LeadingDotSpace,
        TrailingDotSpace,
        ConsecutiveDots,
        NullByte
    };

    struct ValidationError {
        ErrorCode code;
        size_t    position;
    };

    [[nodiscard]] static std::expected<void, ValidationError> validate(std::string_view name) noexcept;

private:
    static constexpr size_t              MAX_NAME_LENGTH = 255;
    static constexpr std::array<char, 9> INVALID_CHARS   = { '<', '>', ':', '"', '/', '\\', '|', '?', '*' };

    static constexpr std::array<std::string_view, 22> RESERVED_NAMES = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };

    [[nodiscard]] static std::expected<void, ValidationError> check_empty(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError> check_length(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError> check_null_byte(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError>
    check_invalid_chars(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError>
    check_control_chars(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError>
    check_boundary_chars(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError>
    check_consecutive_dots(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError>
    check_reserved_name(std::string_view name) noexcept;
};

class PathValidator {
public:
    enum class ErrorCode {
        EmptyPath,
        TooLong,
        InvalidName,
        EmptyComponent,
        InvalidDriveLetter
    };

    struct ValidationError {
        ErrorCode                                     code;
        size_t                                        position;
        std::optional<NameValidator::ValidationError> name_error;
    };

    [[nodiscard]] static std::expected<void, ValidationError> validate(std::string_view path) noexcept;

private:
    static constexpr size_t MAX_PATH_LENGTH = 4096;
};
