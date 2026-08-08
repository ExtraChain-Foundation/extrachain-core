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

#include <cstdint>
#include <expected>
#include <functional>
#include <string>

#include "extrachain_global.h"

namespace DagMigration {

enum class Error {
    NoWorkNeeded,
    AlreadyCurrent,
    DagNotFound,
    ReadFailed,
    WriteFailed,
    ParseFailed,
    PackFailed,
    CheckpointFailed,
    CopyFailed,
    ValidationFailed,
    ActivationFailed,
    BackupExists
};

struct Progress {
    std::uint64_t processed;   // sections converted so far
    std::uint64_t total;       // estimated total sections
    std::string   stage;       // "sections" | "packing" | "balance_cache" | "done"
};

using ProgressCallback = std::function<void(const Progress &)>;

/**
 * Returns true if dag/ on disk contains the pre-decimal, pre-pack layout
 * (hex shard folders containing hex-named section files).
 */
EXTRACHAIN_EXPORT bool needs_migration();

/**
 * Convert legacy hex-indexed DAG into decimal hot/ files and packed cold packs.
 * The conversion runs in a sibling staging directory. The live DAG is replaced
 * only after validation, and the legacy directory remains as a backup.
 * Idempotent — safe to call on already-migrated storage (returns AlreadyCurrent).
 */
EXTRACHAIN_EXPORT std::expected<void, Error>
migrate(ProgressCallback on_progress = {});

} // namespace DagMigration
