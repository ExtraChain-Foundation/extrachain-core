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

#include <ios>
#include <string>

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QMap>
#include <QMessageLogContext>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QString>
#include <QUrl>
#include <QVariant>

#include <fmt/ranges.h>

#include "utils/exc_logs.h"

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
struct fmt::formatter<QVariantMap> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(const QVariantMap& map, FormatContext& ctx) const {
        if (map.isEmpty()) {
            return fmt::format_to(ctx.out(), "{{}}");
        }

        fmt::format_to(ctx.out(), "{{ ");
        for (auto it = map.begin(); it != map.end(); ++it) {
            if (it != map.begin()) {
                fmt::format_to(ctx.out(), ", ");
            }
            fmt::format_to(ctx.out(), "{}: {}", it.key().toStdString(), it.value());
        }
        return fmt::format_to(ctx.out(), " }}");
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
    auto format(const QDateTime& date_time, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", date_time.toString("yyyy-MM-dd HH:mm:ss.zzz").toStdString());
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

template <>
struct fmt::formatter<QNetworkInterface> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(const QNetworkInterface& network_interface, FormatContext& ctx) const {
        std::string flags;
        if (network_interface.flags() & QNetworkInterface::IsUp) {
            flags += "Up|";
        }
        if (network_interface.flags() & QNetworkInterface::IsRunning) {
            flags += "Running|";
        }
        if (network_interface.flags() & QNetworkInterface::CanBroadcast) {
            flags += "Broadcast|";
        }
        if (network_interface.flags() & QNetworkInterface::IsLoopBack) {
            flags += "Loopback|";
        }
        if (!flags.empty()) {
            flags.pop_back();
        }

        return fmt::format_to(ctx.out(),
                              "{}[{}]: {} -> {}",
                              network_interface.name().toStdString(),
                              network_interface.hardwareAddress().toStdString(),
                              flags,
                              fmt::join(network_interface.addressEntries(), ", "));
    }
};

template <>
struct fmt::formatter<QNetworkAddressEntry> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(const QNetworkAddressEntry& address, FormatContext& ctx) const {
        const auto& broadcast = address.broadcast();
        if (broadcast.isNull()) {
            return fmt::format_to(ctx.out(),
                                  "{}/{}",
                                  address.ip().toString().toStdString(),
                                  address.prefixLength());
        }
        return fmt::format_to(ctx.out(),
                              "{}/{} (broadcast: {})",
                              address.ip().toString().toStdString(),
                              address.prefixLength(),
                              broadcast.toString().toStdString());
    }
};

template <>
struct fmt::formatter<QHostAddress> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }

    template <typename FormatContext>
    auto format(const QHostAddress& address, FormatContext& ctx) const {
        if (address.isNull()) {
            return fmt::format_to(ctx.out(), "null");
        }
        return fmt::format_to(ctx.out(), "{}", address.toString().toStdString());
    }
};

inline void reset_qt_log_handler() {
    qInstallMessageHandler(nullptr);
}

inline void install_qt_log_handler() {
    std::ios_base::sync_with_stdio(false);
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext& context, const QString& message) {
        const auto level = type == QtDebugMsg      ? LogLevel::Debug
                           : type == QtWarningMsg  ? LogLevel::Debug
                           : type == QtCriticalMsg ? LogLevel::Critical
                           : type == QtFatalMsg    ? LogLevel::Fatal
                                                   : LogLevel::Info;
        detail::println_impl(level,
                             context.file ? context.file : "FromQt",
                             context.line,
                             "{}",
                             message.toStdString());
    });
}
