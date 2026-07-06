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

#include <string>

// Persists the autologin hash to `.auth_hash` for unattended restarts. Off by
// default: it stores an unencrypted credential on disk, so a caller must opt in
// via set_enabled(true) (e.g. a headless/daemon deployment that needs it).
class AutologinHash {
public:
    static void set_enabled(bool enabled);
    static bool is_enabled();

    bool               load();
    void               save(const std::string &key);
    const std::string &hash() const;

    static bool is_available();

private:
    static bool enabled_;
    std::string hash_;
};
