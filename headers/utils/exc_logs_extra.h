/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#ifndef EXC_LOGS_EXTRA_H
#define EXC_LOGS_EXTRA_H

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <optional>
#include <expected>
#include <variant>
#include <filesystem>
#include "magic_enum.hpp"

template <typename T>
struct fmt::formatter<T, std::enable_if_t<std::is_enum_v<T>, char>> : formatter<std::string_view> {
    template <typename FormatContext>
    auto format(T value, FormatContext& ctx) const {
        std::string_view enum_name  = magic_enum::enum_type_name<T>();
        std::string_view value_name = magic_enum::enum_name(value);
        return formatter<string_view>::format(fmt::format("{}::{}", enum_name, value_name), ctx);
    }
};

template <typename T>
struct fmt::formatter<std::optional<T>> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const std::optional<T>& opt, FormatContext& ctx) const {
        if (opt.has_value()) {
            return fmt::format_to(ctx.out(), "Some({})", opt.value());
        } else {
            return fmt::format_to(ctx.out(), "None");
        }
    }
};

template <typename T, typename E>
struct fmt::formatter<std::expected<T, E>> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const std::expected<T, E>& exp, FormatContext& ctx) const { // Добавлен const
        if (exp.has_value()) {
            if constexpr (std::is_void_v<T>) {
                return fmt::format_to(ctx.out(), "Ok(())");
            } else {
                return fmt::format_to(ctx.out(), "Ok({})", exp.value());
            }
        } else {
            return fmt::format_to(ctx.out(), "Err({})", exp.error());
        }
    }
};

template <typename... Types>
struct fmt::formatter<std::variant<Types...>> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const std::variant<Types...>& var, FormatContext& ctx) const {
        return std::visit(
            [&ctx](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                return fmt::format_to(ctx.out(), "{}({})", std::string_view(typeid(T).name()), value);
            },
            var);
    }
};

template <>
struct fmt::formatter<std::filesystem::path> {
    enum class Style {
        Full,
        Stem,
        Extension,
        Filename
    };
    Style style = Style::Full;

    constexpr auto parse(format_parse_context& ctx) {
        auto it = ctx.begin();
        if (it != ctx.end() && *it != '}') {
            switch (*it) {
            case 's':
                style = Style::Stem;
                break;
            case 'e':
                style = Style::Extension;
                break;
            case 'f':
                style = Style::Filename;
                break;
            default:
                style = Style::Full;
                break;
            }
            ++it;
        }
        return it;
    }

    template <typename FormatContext>
    auto format(const std::filesystem::path& p, FormatContext& ctx) const {
        switch (style) {
        case Style::Stem:
            return fmt::format_to(ctx.out(), "{}", p.stem().string());
        case Style::Extension:
            return fmt::format_to(ctx.out(), "{}", p.extension().string());
        case Style::Filename:
            return fmt::format_to(ctx.out(), "{}", p.filename().string());
        default:
            return fmt::format_to(ctx.out(), "{}", p.string());
        }
    }
};

//
// Qt types support for fmt
//

#include <QString>
#include <QVariant>
#include <QDateTime>
#include <QUrl>
#include <QMap>
#include <QHash>

#include <fmt/ranges.h>

// Qt basic types support for fmt
template <>
struct fmt::formatter<QString> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QString& str, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", str.toStdString());
    }
};

template <>
struct fmt::formatter<QByteArray> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QByteArray& bytes, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", bytes.toStdString());
    }
};

template <>
struct fmt::formatter<QVariant> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QVariant& var, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", var.toString().toStdString());
    }
};

template <>
struct fmt::formatter<QTime> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QTime& time, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", time.toString("HH:mm:ss.zzz").toStdString());
    }
};

template <>
struct fmt::formatter<QDate> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QDate& date, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", date.toString("yyyy-MM-dd").toStdString());
    }
};

template <>
struct fmt::formatter<QDateTime> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QDateTime& dt, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", dt.toString("yyyy-MM-dd HH:mm:ss.zzz").toStdString());
    }
};

template <>
struct fmt::formatter<QUrl> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const QUrl& url, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", url.toString().toStdString());
    }
};

#endif // EXC_LOGS_EXTRA_H
