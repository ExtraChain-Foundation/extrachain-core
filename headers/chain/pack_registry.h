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

#include <cstddef>
#include <deque>
#include <expected>
#include <filesystem>
#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "chain/pack.h"
#include "extrachain_global.h"
#include "utils/bignumber.h"

namespace Pack {

    /**
     * Lazily opens pack files from a directory, keeps recently used ones in memory.
     * Thread-safe: reads take a shared lock, opens take an exclusive one.
     *
     * Directory layout: <dir>/<pack_id>.pack where pack_id is decimal uint64.
     */
    class EXTRACHAIN_EXPORT Registry {
    public:
        explicit Registry(std::filesystem::path packs_dir, std::size_t max_open = 16);
        ~Registry();

        Registry(const Registry &)            = delete;
        Registry &operator=(const Registry &) = delete;

        // Scan directory for .pack files, load their headers only (no data).
        // Call once at startup (or after external changes).
        void rescan();

        // Returns pack_id that contains given section, if any (requires rescan first).
        std::optional<PackId> find_pack_for_section(const SectionId &id) const;

        // Read one section. Opens pack lazily.
        std::optional<std::string> read_section(const SectionId &id);

        // Read a range spanning possibly multiple packs.
        std::vector<std::pair<SectionId, std::string>> read_sections(const SectionId &from, const SectionId &to);

        // Create a new pack file in the managed directory.
        std::expected<void, Error> create_pack(PackId pack_id, const std::map<SectionId, std::string> &sections);

        // Ordered list of known pack ids
        std::vector<PackId> known_packs() const;

        // Returns range covered by known packs, or nullopt if empty.
        struct Range {
            SectionId first;
            SectionId last;
        };
        std::optional<Range> coverage() const;

        // Id + section range of every known pack, straight from in-memory metadata
        // (populated by rescan()/create_pack()/install_raw()). Cheap — no file I/O,
        // so callers building a pack list don't re-open and re-checksum every file.
        struct Span {
            PackId    id;
            SectionId first;
            SectionId last;
        };
        std::vector<Span> spans() const;

        const std::filesystem::path &dir() const;

        // Read the raw on-disk bytes of a pack — used by network sync to ship the
        // file as-is. Returns nullopt if the pack is unknown or unreadable.
        std::optional<std::string> read_raw(PackId id) const;

        // Atomically install a pack received from a peer. Validates by opening it
        // (Pack::Reader::open: magic + version + bounds + Blake3 footer checksum).
        // Existing pack with same id is overwritten.
        std::expected<void, Error> install_raw(PackId id, std::string_view bytes);

        // Byte size of a pack file on disk, or nullopt if unknown/unreadable.
        std::optional<std::uint64_t> pack_byte_size(PackId id) const;

        // Read a slice of a pack file [offset, offset+len) without loading the whole
        // file — keeps pack sync memory bounded to one chunk. Returns the bytes read
        // (may be shorter than len at EOF), or nullopt on error.
        std::optional<std::string> read_chunk(PackId id, std::uint64_t offset, std::size_t len) const;

        // Streaming install: append a received chunk to the pack's .incoming file at
        // offset. When is_last is set, the completed file is validated and atomically
        // swapped in (same checks as install_raw). Keeps memory bounded to one chunk.
        std::expected<void, Error> install_chunk(PackId           id,
                                                 std::uint64_t    offset,
                                                 std::string_view bytes,
                                                 bool             is_last);

        // Remove an incomplete network transfer for this pack.
        void discard_incoming(PackId id);

    private:
        struct PackMeta {
            PackId    id;
            SectionId first;
            SectionId last;
        };

        std::filesystem::path dir_;
        std::size_t           max_open_;

        mutable std::shared_mutex meta_mutex_;
        std::vector<PackMeta>     meta_; // sorted by first

        mutable std::mutex                        cache_mutex_;
        std::map<PackId, std::unique_ptr<Reader>> readers_;
        std::list<PackId>                         lru_;

        // Returns iterator into meta_ or meta_.end() (requires shared lock).
        std::vector<PackMeta>::const_iterator find_meta_for(std::uint64_t section) const;

        // Open/read with LRU bookkeeping. Must hold cache_mutex_.
        Reader *acquire_reader_locked(PackId id);
        void    evict_if_needed_locked();

        std::filesystem::path pack_path(PackId id) const;

        // Validate a completed .incoming file for id and atomically swap it into
        // place, updating meta_. Removes the temp file on any failure.
        std::expected<void, Error> finalize_incoming(PackId id, const std::filesystem::path &tmp);

        static std::optional<PackId> parse_pack_filename(const std::filesystem::path &p);
    };

} // namespace Pack
