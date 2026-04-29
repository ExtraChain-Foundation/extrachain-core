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

#include "chain/pack_registry.h"

#include <algorithm>
#include <charconv>
#include <system_error>

#include "utils/exc_logs.h"

namespace Pack {

namespace {

std::uint64_t section_to_u64(const SectionId &id) {
    auto i = id.to_int();
    if (i.has_value() && *i >= 0) return static_cast<std::uint64_t>(*i);
    return std::stoull(id.to_string());
}

} // namespace

Registry::Registry(std::filesystem::path packs_dir, std::size_t max_open)
    : dir_(std::move(packs_dir))
    , max_open_(max_open) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
}

Registry::~Registry() = default;

std::optional<PackId> Registry::parse_pack_filename(const std::filesystem::path &p) {
    if (p.extension() != ".pack") return std::nullopt;
    auto stem = p.stem().string();
    if (stem.empty()) return std::nullopt;

    std::uint64_t id = 0;
    auto [ptr, ec]   = std::from_chars(stem.data(), stem.data() + stem.size(), id);
    if (ec != std::errc{} || ptr != stem.data() + stem.size()) return std::nullopt;
    return id;
}

std::filesystem::path Registry::pack_path(PackId id) const {
    return dir_ / (std::to_string(id) + ".pack");
}

void Registry::rescan() {
    std::vector<PackMeta> discovered;

    std::error_code ec;
    if (!std::filesystem::exists(dir_, ec)) {
        std::unique_lock lock(meta_mutex_);
        meta_.clear();
        return;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        auto pid = parse_pack_filename(entry.path());
        if (!pid.has_value()) continue;

        auto r = Reader::open(entry.path());
        if (!r.has_value()) {
            eWarning("[PackRegistry] Skip broken pack {}: error {}",
                     entry.path().string(), static_cast<int>(r.error()));
            continue;
        }
        discovered.push_back(PackMeta {
            .id    = *pid,
            .first = r->first_section(),
            .last  = r->last_section(),
        });
    }

    std::sort(discovered.begin(), discovered.end(),
              [](const PackMeta &a, const PackMeta &b) { return a.first < b.first; });

    {
        std::unique_lock lock(meta_mutex_);
        meta_ = std::move(discovered);
    }

    // Drop any cached readers whose pack id is no longer present
    {
        std::lock_guard cache_lock(cache_mutex_);
        std::vector<PackId> valid;
        {
            std::shared_lock meta_lock(meta_mutex_);
            valid.reserve(meta_.size());
            for (const auto &m : meta_) valid.push_back(m.id);
        }
        std::sort(valid.begin(), valid.end());
        for (auto it = readers_.begin(); it != readers_.end();) {
            if (!std::binary_search(valid.begin(), valid.end(), it->first)) {
                lru_.remove(it->first);
                it = readers_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

std::vector<Registry::PackMeta>::const_iterator
Registry::find_meta_for(std::uint64_t section) const {
    // meta_ sorted by first; find rightmost meta with first <= section
    auto it = std::upper_bound(meta_.begin(), meta_.end(), section,
                               [](std::uint64_t s, const PackMeta &m) {
                                   return s < section_to_u64(m.first);
                               });
    if (it == meta_.begin()) return meta_.end();
    --it;
    std::uint64_t last = section_to_u64(it->last);
    if (section > last) return meta_.end();
    return it;
}

std::optional<PackId> Registry::find_pack_for_section(const SectionId &id) const {
    std::uint64_t s = section_to_u64(id);
    std::shared_lock lock(meta_mutex_);
    auto it = find_meta_for(s);
    if (it == meta_.end()) return std::nullopt;
    return it->id;
}

Reader *Registry::acquire_reader_locked(PackId id) {
    auto it = readers_.find(id);
    if (it != readers_.end()) {
        // touch LRU
        lru_.remove(id);
        lru_.push_front(id);
        return it->second.get();
    }

    auto r = Reader::open(pack_path(id));
    if (!r.has_value()) {
        eWarning("[PackRegistry] Failed to open {}: error {}",
                 pack_path(id).string(), static_cast<int>(r.error()));
        return nullptr;
    }

    auto [inserted_it, _] =
        readers_.emplace(id, std::make_unique<Reader>(std::move(*r)));
    lru_.push_front(id);

    evict_if_needed_locked();
    return inserted_it->second.get();
}

void Registry::evict_if_needed_locked() {
    while (readers_.size() > max_open_ && !lru_.empty()) {
        PackId victim = lru_.back();
        lru_.pop_back();
        readers_.erase(victim);
    }
}

std::optional<std::string> Registry::read_section(const SectionId &id) {
    std::uint64_t s = section_to_u64(id);

    PackId pid;
    {
        std::shared_lock lock(meta_mutex_);
        auto it = find_meta_for(s);
        if (it == meta_.end()) return std::nullopt;
        pid = it->id;
    }

    std::lock_guard cache_lock(cache_mutex_);
    Reader *reader = acquire_reader_locked(pid);
    if (!reader) return std::nullopt;
    return reader->read(id);
}

std::vector<std::pair<SectionId, std::string>>
Registry::read_sections(const SectionId &from, const SectionId &to) {
    std::vector<std::pair<SectionId, std::string>> out;
    if (from > to) return out;

    std::uint64_t lo = section_to_u64(from);
    std::uint64_t hi = section_to_u64(to);

    std::vector<PackId> packs_needed;
    {
        std::shared_lock lock(meta_mutex_);
        for (const auto &m : meta_) {
            std::uint64_t m_first = section_to_u64(m.first);
            std::uint64_t m_last  = section_to_u64(m.last);
            if (m_last < lo) continue;
            if (m_first > hi) break;
            packs_needed.push_back(m.id);
        }
    }

    for (PackId pid : packs_needed) {
        std::lock_guard cache_lock(cache_mutex_);
        Reader *reader = acquire_reader_locked(pid);
        if (!reader) continue;

        std::uint64_t m_first = section_to_u64(reader->first_section());
        std::uint64_t m_last  = section_to_u64(reader->last_section());
        std::uint64_t r_lo    = std::max(lo, m_first);
        std::uint64_t r_hi    = std::min(hi, m_last);

        auto chunk = reader->read_range(SectionId(static_cast<long long>(r_lo)),
                                         SectionId(static_cast<long long>(r_hi)));
        for (auto &kv : chunk) out.push_back(std::move(kv));
    }

    return out;
}

std::expected<void, Error>
Registry::create_pack(PackId pack_id, const std::map<SectionId, std::string> &sections) {
    auto path = pack_path(pack_id);
    auto res  = Pack::write(path, pack_id, sections);
    if (!res.has_value()) return res;

    // Update metadata
    auto r = Reader::open(path);
    if (!r.has_value()) return std::unexpected(r.error());

    PackMeta m { .id = pack_id, .first = r->first_section(), .last = r->last_section() };
    {
        std::unique_lock lock(meta_mutex_);
        // Remove any existing meta with same id
        meta_.erase(std::remove_if(meta_.begin(), meta_.end(),
                                    [&](const PackMeta &x) { return x.id == pack_id; }),
                    meta_.end());
        meta_.push_back(m);
        std::sort(meta_.begin(), meta_.end(),
                  [](const PackMeta &a, const PackMeta &b) { return a.first < b.first; });
    }
    return {};
}

std::vector<PackId> Registry::known_packs() const {
    std::shared_lock lock(meta_mutex_);
    std::vector<PackId> ids;
    ids.reserve(meta_.size());
    for (const auto &m : meta_) ids.push_back(m.id);
    return ids;
}

std::optional<Registry::Range> Registry::coverage() const {
    std::shared_lock lock(meta_mutex_);
    if (meta_.empty()) return std::nullopt;
    return Range { .first = meta_.front().first, .last = meta_.back().last };
}

const std::filesystem::path &Registry::dir() const {
    return dir_;
}

std::optional<std::string> Registry::read_raw(PackId id) const {
    auto path = pack_path(id);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::nullopt;

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::nullopt;
    auto size = f.tellg();
    if (size < 0) return std::nullopt;
    f.seekg(0);
    std::string out;
    out.resize(static_cast<std::size_t>(size));
    if (!f.read(out.data(), static_cast<std::streamsize>(out.size()))) {
        return std::nullopt;
    }
    return out;
}

std::expected<void, Error> Registry::install_raw(PackId id, std::string_view bytes) {
    if (bytes.empty()) return std::unexpected(Error::EmptyInput);

    auto target = pack_path(id);
    auto tmp    = target;
    tmp += ".incoming";

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return std::unexpected(Error::WriteFailed);
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!f) return std::unexpected(Error::WriteFailed);
    }

    // Validate by opening; reject corrupt payloads before swapping in.
    auto check = Reader::open(tmp);
    if (!check.has_value()) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return std::unexpected(check.error());
    }
    if (check->id() != id) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error::InvalidFormat);
    }

    PackMeta meta { .id = id, .first = check->first_section(), .last = check->last_section() };

    // Drop any cached reader for this id before overwriting the file (mmap on
    // some platforms keeps a hold on the path).
    {
        std::lock_guard cache_lock(cache_mutex_);
        readers_.erase(id);
        lru_.remove(id);
    }

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error::WriteFailed);
    }

    {
        std::unique_lock lock(meta_mutex_);
        meta_.erase(std::remove_if(meta_.begin(), meta_.end(),
                                    [&](const PackMeta &m) { return m.id == id; }),
                    meta_.end());
        meta_.push_back(meta);
        std::sort(meta_.begin(), meta_.end(),
                  [](const PackMeta &a, const PackMeta &b) { return a.first < b.first; });
    }
    return {};
}

} // namespace Pack
