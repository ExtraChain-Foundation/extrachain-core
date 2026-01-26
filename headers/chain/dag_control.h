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

#include "utils/bignumber.h"

static const SectionId CONTROL_INTERVAL      = SectionId(20);
static const int       CONTROL_INTERVAL_MOD  = 20;
static const SectionId CONTROL_INTERVAL_DIFF = CONTROL_INTERVAL - 1; // 19

static inline bool is_aligned20(const SectionId &s) {
    return (s % CONTROL_INTERVAL) == 0;
}

static inline SectionId align_down20(const SectionId &s) {
    eLog("align_down20 {}", s);
    SectionId m;
    m = s % CONTROL_INTERVAL;
    return m == 0 ? s : (s - m);
}

static inline std::vector<SectionId> control_ids_in(SectionId from, SectionId to) {
    std::vector<SectionId> v;
    for (SectionId s = from; s <= to; s += CONTROL_INTERVAL_MOD)
        v.push_back(s);
    return v;
}
