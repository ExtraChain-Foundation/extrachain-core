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

#include <QString>
#include <QtNetwork/QNetworkAddressEntry>

#include "adapters/qt/qt_compat_global.h"
#include "core/types.h"

namespace ExtraChain::Qt {

    EXTRACHAIN_QT_EXPORT QString              data_dir(const QString &new_dir = {});
    EXTRACHAIN_QT_EXPORT qint64               disk_free_memory();
    EXTRACHAIN_QT_EXPORT QString              mime_type(const QString &file_path);
    EXTRACHAIN_QT_EXPORT QString              preferred_mime_suffix(const QString &file_path);
    EXTRACHAIN_QT_EXPORT QString              compiler_info();
    EXTRACHAIN_QT_EXPORT QNetworkAddressEntry local_ip(bool debug = false);
    EXTRACHAIN_QT_EXPORT QString sanitize_file_name(const QString &file_name, const QString &replace_symbol = "_");
    EXTRACHAIN_QT_EXPORT bool    valid_ip(const QString &ip);

} // namespace ExtraChain::Qt
