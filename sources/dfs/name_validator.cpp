#include "dfs/name_validator.h"
#include <algorithm>
#include <ranges>

std::expected<void, NameValidator::ValidationError>
NameValidator::check_empty(std::string_view name) noexcept {
    if (name.empty()) {
        return std::unexpected(ValidationError{
            .code = ErrorCode::EmptyName,
            .position = 0
        });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::check_length(std::string_view name) noexcept {
    if (name.length() > MAX_NAME_LENGTH) {
        return std::unexpected(ValidationError{
            .code = ErrorCode::TooLong,
            .position = MAX_NAME_LENGTH
        });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::check_null_byte(std::string_view name) noexcept {
    if (auto pos = name.find('\0'); pos != std::string_view::npos) {
        return std::unexpected(ValidationError{
            .code = ErrorCode::NullByte,
            .position = pos
        });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::check_invalid_chars(std::string_view name) noexcept {
    for (size_t i = 0; i < name.length(); ++i) {
        if (std::ranges::find(INVALID_CHARS, name[i]) != INVALID_CHARS.end()) {
            return std::unexpected(ValidationError{
                .code = ErrorCode::InvalidChar,
                .position = i
            });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::check_control_chars(std::string_view name) noexcept {
    for (size_t i = 0; i < name.length(); ++i) {
        if (static_cast<unsigned char>(name[i]) < 32) {
            return std::unexpected(ValidationError{
                .code = ErrorCode::ControlChar,
                .position = i
            });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::check_boundary_chars(std::string_view name) noexcept {
    if (name.front() == '.' || name.front() == ' ') {
        return std::unexpected(ValidationError{
            .code = ErrorCode::LeadingDotSpace,
            .position = 0
        });
    }
    if (name.back() == '.' || name.back() == ' ') {
        return std::unexpected(ValidationError{
            .code = ErrorCode::TrailingDotSpace,
            .position = name.length() - 1
        });
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::check_consecutive_dots(std::string_view name) noexcept {
    for (size_t i = 0; i < name.length() - 1; ++i) {
        if (name[i] == '.' && name[i + 1] == '.') {
            return std::unexpected(ValidationError{
                .code = ErrorCode::ConsecutiveDots,
                .position = i
            });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::check_reserved_name(std::string_view name) noexcept {
    std::string upper_name;
    upper_name.reserve(name.size());
    std::transform(name.begin(), name.end(), std::back_inserter(upper_name),
                   [](unsigned char c) { return std::toupper(c); });

    auto dot_pos = upper_name.find('.');
    std::string_view base_name(upper_name.data(),
                               dot_pos == std::string::npos ? upper_name.length() : dot_pos);

    for (const auto& reserved : RESERVED_NAMES) {
        if (base_name == reserved) {
            return std::unexpected(ValidationError{
                .code = ErrorCode::ReservedName,
                .position = 0
            });
        }
    }
    return {};
}

std::expected<void, NameValidator::ValidationError>
NameValidator::validate(std::string_view name) noexcept {
    if (auto result = check_empty(name); !result) return result;
    if (auto result = check_length(name); !result) return result;
    if (auto result = check_null_byte(name); !result) return result;
    if (auto result = check_invalid_chars(name); !result) return result;
    if (auto result = check_control_chars(name); !result) return result;
    if (auto result = check_boundary_chars(name); !result) return result;
    if (auto result = check_consecutive_dots(name); !result) return result;
    if (auto result = check_reserved_name(name); !result) return result;

    return {};
}
