/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "adapters/qt/utils_adapter.h"

#include <QHostAddress>
#include <QMimeDatabase>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTcpSocket>

#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace ExtraChain::Qt {

    QString data_dir(const QString &new_dir) {
        static QString current = "extrachain-data";
        if (!new_dir.isEmpty()) {
            current = sanitize_file_name(new_dir);
        }
        return current;
    }

    qint64 disk_free_memory() {
        return static_cast<qint64>(Utils::diskFreeMemory());
    }

    QString mime_type(const QString &file_path) {
        return QMimeDatabase().mimeTypeForFile(file_path).name();
    }

    QString preferred_mime_suffix(const QString &file_path) {
        return QMimeDatabase().mimeTypeForFile(file_path).preferredSuffix();
    }

    QString compiler_info() {
        return QString::fromStdString(Utils::detect_compiler());
    }

    QNetworkAddressEntry local_ip(bool debug) {
        const auto interfaces = QNetworkInterface::allInterfaces();
        for (const auto &network_interface : interfaces) {
            const auto flags = network_interface.flags();
            if (!flags.testFlag(QNetworkInterface::IsRunning) || flags.testFlag(QNetworkInterface::IsLoopBack)
                || flags.testFlag(QNetworkInterface::IsPointToPoint)) {
                continue;
            }
            for (const auto &entry : network_interface.addressEntries()) {
                if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) {
                    continue;
                }
                QTcpSocket socket;
                socket.bind(entry.ip());
                socket.connectToHost("1.1.1.1", 53);
                if (socket.waitForConnected(1000)) {
                    if (debug) {
                        eLog("[FindLocalIp] Selected {} on {}",
                             entry.ip().toString().toStdString(),
                             network_interface.name().toStdString());
                    }
                    return entry;
                }
            }
        }
        eWarning("[Network] Local IPv4 address is not available");
        QNetworkAddressEntry fallback;
        fallback.setIp(QHostAddress::AnyIPv4);
        return fallback;
    }

    QString sanitize_file_name(const QString &file_name, const QString &replace_symbol) {
        auto fixed = file_name.simplified();
        fixed.replace(QRegularExpression("[+%@!:*?/\"<>|«»]+"), replace_symbol);
        fixed.replace("\\", replace_symbol);
        return fixed;
    }

    bool valid_ip(const QString &ip) {
        const QHostAddress address(ip);
        return address.protocol() == QAbstractSocket::IPv4Protocol
               || address.protocol() == QAbstractSocket::IPv6Protocol;
    }

} // namespace ExtraChain::Qt
