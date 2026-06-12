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

/**
 * Wire-format selector consulted by msgpack_pack on BigNumber / BigNumberFloat.
 *
 * The surrounding protocol logic (e.g. NetworkManager preparing a payload
 * for a legacy peer) sets Legacy via WireFormat::Scope, serialises, and the
 * scope restores Canonical on destruction. Serialization is done synchronously
 * within the same thread so a thread-local flag is sufficient and isolated.
 *
 * Legacy  = pre-decimal wire: BigNumber/Float serialise via to_hex_string().
 *           Used when talking to peers that predate dag_version advertisement.
 * Canonical = decimal strings (default).
 */
namespace WireFormat {

enum class Mode {
    Canonical,
    Legacy
};

Mode get_mode();
void set_mode(Mode m);

// TEMPORARY 0.26 legacy compat: hex on the wire until all nodes are >= 0.26.
//
// While ANY pre-decimal peer may still be on the network we must speak hex on
// the wire, because a broadcast/relayed message is a single signed blob that
// passes through nodes which cannot transcode it, and legacy peers can only
// parse hex. So senders serialise payloads in Legacy(hex) and receivers parse
// them in Legacy(hex) — one symmetric, hop-independent format.
//
// On-disk storage stays Canonical(decimal) regardless (see DagMigration).
// Flip this to Mode::Canonical once the whole network advertises
// dag_version >= CURRENT_DAG_VERSION and legacy peers are gone; at that point
// the per-peer PeerMeta machinery can drive a graceful per-link switch.
constexpr Mode wire() {
    return Mode::Legacy;
}

class Scope {
public:
    explicit Scope(Mode m) : prev_(get_mode()) { set_mode(m); }
    ~Scope() { set_mode(prev_); }
    Scope(const Scope &)            = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Mode prev_;
};

} // namespace WireFormat
