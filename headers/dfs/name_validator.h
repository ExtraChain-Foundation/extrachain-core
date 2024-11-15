#pragma once

#include <string_view>
#include <array>
#include <expected>

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
        size_t position;
    };

    [[nodiscard]] static std::expected<void, ValidationError>
    validate(std::string_view name) noexcept;

private:
    static constexpr size_t MAX_NAME_LENGTH = 255;
    static constexpr std::array<char, 9> INVALID_CHARS = {
        '<', '>', ':', '"', '/', '\\', '|', '?', '*'
    };

    static constexpr std::array<std::string_view, 22> RESERVED_NAMES = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };

    [[nodiscard]] static std::expected<void, ValidationError>
    check_empty(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError>
    check_length(std::string_view name) noexcept;

    [[nodiscard]] static std::expected<void, ValidationError>
    check_null_byte(std::string_view name) noexcept;

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
