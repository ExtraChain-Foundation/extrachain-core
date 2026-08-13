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

#ifndef EXTRACHAIN_CONTROL_INDEX_H
#define EXTRACHAIN_CONTROL_INDEX_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "extrachain_global.h"
#include "utils/bignumber.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}

/**
 * Read-side accelerator for control hashes.
 *
 * Control hashes are still stored in their sections (consensus + wire stay
 * unchanged); this index is a rebuildable cache mapping section_id -> hash so
 * find_last_control()/read_control() become O(1) lookups instead of walking
 * and decompressing whole section frames. dag/cache/Control.db, droppable.
 *
 * Not part of consensus: if the file is missing or stale, rebuild_from_dag()
 * repopulates it from the on-disk control sections.
 */
class EXTRACHAIN_EXPORT ControlIndex {
public:
    explicit ControlIndex(ExtraChain::Core::ExtraChainNode *node);
    ~ControlIndex();

    ControlIndex(const ControlIndex &)            = delete;
    ControlIndex &operator=(const ControlIndex &) = delete;

    // Record (or replace) the control hash for a section. Mirrors write_control.
    void put(const SectionId &section_id, const std::string &hash);

    // Drop the control hash for a section (mirrors remove_control / invalidation).
    void erase(const SectionId &section_id);

    // Exact lookup. nullopt if absent.
    std::optional<std::string> get(const SectionId &section_id) const;

    // Highest section_id <= `from` that has a control hash, or nullopt.
    // `from` < 0 means "from the highest known". This is the index-backed
    // equivalent of Dag::find_last_control's backward walk.
    std::optional<std::pair<SectionId, std::string>> last_at_or_below(const SectionId &from) const;

    // Rebuild the whole index by scanning control sections on disk. Blocking.
    void rebuild_from_dag();

    // Drop all rows (wipe flows).
    void clear();

    std::uint64_t row_count() const;
    std::uint64_t row_count_at_or_below(const SectionId &section_id) const;
    bool          rebuild_required() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // EXTRACHAIN_CONTROL_INDEX_H
