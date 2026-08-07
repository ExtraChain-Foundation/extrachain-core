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

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "extrachain_global.h"
#include "utils/bignumber.h"

/**
 * Mutable storage for the hot DAG tail.
 *
 * SQLite WAL avoids one filesystem create operation per section. Each put is
 * still an independent transaction. A process restart therefore exposes only
 * complete section revisions. Cold sections move to immutable Pack storage.
 */
class EXTRACHAIN_EXPORT HotSectionStore {
public:
    explicit HotSectionStore(const std::filesystem::path &path);
    ~HotSectionStore();

    HotSectionStore(const HotSectionStore &)            = delete;
    HotSectionStore &operator=(const HotSectionStore &) = delete;

    bool is_open() const;

    bool                       put(const SectionId &section, const std::string &payload);
    bool                       put_many(const std::map<SectionId, std::string> &sections);
    std::optional<std::string> get(const SectionId &section) const;
    bool                       contains(const SectionId &section) const;

    std::map<SectionId, std::string>               read_range(const SectionId &from, const SectionId &to) const;
    std::optional<std::pair<SectionId, SectionId>> bounds() const;

    bool erase_range(const SectionId &from, const SectionId &to);
    bool erase_from(const SectionId &from);
    bool clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
