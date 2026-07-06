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

#include "utils/autologinhash.h"

#include <QFile>

#include "utils/exc_logs.h"

bool AutologinHash::enabled_ = false;

void AutologinHash::set_enabled(bool enabled) {
    enabled_ = enabled;
}

bool AutologinHash::is_enabled() {
    return enabled_;
}

bool AutologinHash::load() {
    if (!enabled_) {
        return false;
    }

    if (!QFile::exists(".auth_hash")) {
        return false;
    }

    QFile file(".auth_hash");
    if (!file.open(QFile::ReadOnly)) {
        eLog("[Autologin Hash] Can't read auth hash file");
        return false;
    }

    hash_ = file.read(64);
    file.close();
    return hash_.size() == 64;
}

void AutologinHash::save(const std::string& hash) {
    if (!enabled_) {
        return;
    }

    auto  hash_bytes = QByteArray::fromStdString(hash);
    QFile file(".auth_hash");

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        eFatal("[Autologin Hash] Can't write to auth hash file");
        return;
    }

    file.write(hash_bytes);
    file.close();

    hash_ = hash;
}

const std::string& AutologinHash::hash() const {
    return hash_;
}

bool AutologinHash::is_available() {
    AutologinHash hash;
    return hash.load();
}
