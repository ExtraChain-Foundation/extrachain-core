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

#include "chain/pack.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

#include "utils/compression.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace Pack {

namespace {

// Binary on-disk layout:
//   [HEADER 104 bytes]
//   [DICT dict_size bytes]
//   [DATA data_size bytes]              (concatenated zstd frames)
//   [FRAME_INDEX frame_count*24 bytes]  {offset u64, size u32, count u32, first_sec u64}
//   [FOOTER 68 bytes]                   blake3_hex[64] + magic[4]
//
// Section IDs in a pack are consecutive: first..last.
// Lookup: binary search frame_index by first_sec, decompress frame, index inside.
// Inside a decompressed frame: [len u32][bytes][len u32][bytes]...

#pragma pack(push, 1)
struct Header {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint64_t pack_id;
    std::uint64_t first_section;
    std::uint64_t last_section;
    std::uint32_t frame_count;
    std::uint32_t reserved32;
    std::uint64_t dict_offset;
    std::uint64_t dict_size;
    std::uint64_t data_offset;
    std::uint64_t data_size;
    std::uint64_t frame_index_offset;
    std::uint64_t frame_index_size;
    std::uint64_t reserved_a;
    std::uint64_t reserved_b;
};
static_assert(sizeof(Header) == 104, "Header size");

struct FrameEntry {
    std::uint64_t offset;        // offset into data region
    std::uint32_t size;          // compressed size
    std::uint32_t count;         // number of sections in this frame
    std::uint64_t first_section; // first section id in this frame
};
static_assert(sizeof(FrameEntry) == 24, "FrameEntry size");
#pragma pack(pop)

constexpr std::size_t FOOTER_SIZE = 64 + 4;

std::uint64_t section_to_u64(const SectionId &id) {
    auto i = id.to_int();
    if (i.has_value() && *i >= 0) return static_cast<std::uint64_t>(*i);
    return std::stoull(id.to_string());
}

std::string build_dict(const std::map<SectionId, std::string> &sections) {
    static const std::string_view STRUCTURAL[] = {
        std::string_view(R"({"transactions":[{"section":")"),
        std::string_view(R"(","type":)"),
        std::string_view(R"(,"sender":")"),
        std::string_view(R"(,"receiver":")"),
        std::string_view(R"(,"token":")"),
        std::string_view(R"(,"amount":")"),
        std::string_view(R"(,"timestamp":)"),
        std::string_view(R"(,"prev_hashs":[)"),
        std::string_view(R"(,"hash":")"),
        std::string_view(R"(,"signature":")"),
        std::string_view(R"(,"meta":")"),
        std::string_view(R"(}],"control":")"),
        std::string_view(R"("}]})"),
        std::string_view("0000000000000000000000000000000000000000"),
    };

    std::string dict;
    for (const auto &s : STRUCTURAL) dict.append(s);

    std::set<std::string> tokens;
    for (const auto &[_, payload] : sections) {
        std::size_t i = 0;
        while (i < payload.size()) {
            auto q1 = payload.find('"', i);
            if (q1 == std::string::npos) break;
            auto q2 = payload.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            if (q2 - q1 == 41) {
                std::string s = payload.substr(q1 + 1, 40);
                bool hex_like = !s.empty();
                for (char c : s) {
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                        hex_like = false;
                        break;
                    }
                }
                if (hex_like) tokens.insert(std::move(s));
            }
            i = q2 + 1;
        }
    }
    for (const auto &t : tokens) dict.append(t);

    return dict;
}

bool verify_consecutive(const std::map<SectionId, std::string> &sections,
                        SectionId &first_out, SectionId &last_out) {
    if (sections.empty()) return false;
    first_out           = sections.begin()->first;
    last_out            = sections.rbegin()->first;
    SectionId expected  = first_out;
    for (const auto &[id, _] : sections) {
        if (id != expected) return false;
        expected = expected + 1;
    }
    return true;
}

std::string compute_checksum(const char *data, std::size_t size) {
    // Hash in place — avoids copying the whole pack (minus footer) on every open.
    return Utils::calculate_hash_bytes(data, size);
}

std::optional<std::string>
extract_section(const std::string &frame, std::uint64_t target_id,
                std::uint64_t first_id, std::uint32_t count) {
    std::size_t pos = 0;
    for (std::uint64_t sid = first_id; sid < first_id + count && pos < frame.size(); ++sid) {
        if (pos + 4 > frame.size()) return std::nullopt;
        std::uint32_t len;
        std::memcpy(&len, frame.data() + pos, 4);
        pos += 4;
        if (pos + len > frame.size()) return std::nullopt;
        if (sid == target_id) return frame.substr(pos, len);
        pos += len;
    }
    return std::nullopt;
}

} // namespace

struct Reader::Impl {
    std::filesystem::path      path;
    std::vector<std::uint8_t>  buffer;
    Header                     header{};
    std::string_view           dict;
    std::vector<FrameEntry>    frame_index;
    mutable std::unique_ptr<Compression::Context> ctx;

    const char *data_ptr() const { return reinterpret_cast<const char *>(buffer.data()); }

    bool load(const std::filesystem::path &p) {
        path = p;

        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f) return false;
        std::streamsize sz = f.tellg();
        if (sz < static_cast<std::streamsize>(sizeof(Header) + FOOTER_SIZE)) return false;
        f.seekg(0);
        buffer.resize(sz);
        if (!f.read(reinterpret_cast<char *>(buffer.data()), sz)) return false;

        std::memcpy(&header, buffer.data(), sizeof(Header));
        if (header.magic != MAGIC) return false;
        if (header.version != FORMAT_VERSION) return false;

        auto end = buffer.size();
        // A pack can arrive from an untrusted peer, so header fields are hostile.
        // Compare as (limit - offset) to avoid the offset+size sum wrapping past
        // 2^64 and passing a naive `offset + size > end` check.
        auto fits = [end](std::uint64_t offset, std::uint64_t size) {
            return offset <= end && size <= end - offset;
        };
        if (!fits(header.dict_offset, header.dict_size)) return false;
        if (!fits(header.data_offset, header.data_size)) return false;
        if (!fits(header.frame_index_offset, header.frame_index_size)) return false;
        // frame_count * sizeof(FrameEntry) must not overflow before comparison.
        if (header.frame_count > header.frame_index_size / sizeof(FrameEntry)) return false;
        if (header.frame_index_size != header.frame_count * sizeof(FrameEntry)) return false;

        if (end < FOOTER_SIZE) return false;
        auto footer_off = end - FOOTER_SIZE;
        std::uint32_t footer_magic;
        std::memcpy(&footer_magic, buffer.data() + footer_off + 64, 4);
        if (footer_magic != MAGIC) return false;

        auto expected = compute_checksum(data_ptr(), footer_off);
        std::string stored(reinterpret_cast<const char *>(buffer.data() + footer_off), 64);
        if (expected != stored) return false;

        dict = std::string_view(data_ptr() + header.dict_offset, header.dict_size);

        frame_index.resize(header.frame_count);
        if (header.frame_count > 0) {
            std::memcpy(frame_index.data(),
                        buffer.data() + header.frame_index_offset,
                        header.frame_index_size);
        }

        ctx = std::make_unique<Compression::Context>(dict, COMPRESSION_LEVEL);
        return true;
    }

    std::ptrdiff_t find_frame(std::uint64_t id) const {
        if (frame_index.empty()) return -1;
        std::size_t lo = 0, hi = frame_index.size();
        while (lo < hi) {
            std::size_t mid = lo + (hi - lo) / 2;
            if (frame_index[mid].first_section <= id) lo = mid + 1;
            else hi = mid;
        }
        if (lo == 0) return -1;
        std::size_t idx = lo - 1;
        const auto  &fe = frame_index[idx];
        if (id >= fe.first_section + fe.count) return -1;
        return static_cast<std::ptrdiff_t>(idx);
    }

    std::optional<std::string> decompress_frame(std::size_t frame_idx) const {
        if (frame_idx >= frame_index.size()) return std::nullopt;
        const auto &fe = frame_index[frame_idx];
        // fe.offset/fe.size are attacker-controlled on an untrusted pack; keep the
        // frame window inside the validated data region (offset+size within data_size).
        if (fe.offset > header.data_size || fe.size > header.data_size - fe.offset) {
            return std::nullopt;
        }
        std::string_view frame_data(data_ptr() + header.data_offset + fe.offset, fe.size);
        auto out = ctx->decompress_frame(frame_data);
        if (!out.has_value()) return std::nullopt;
        return *out;
    }
};

Reader::Reader()                                    = default;
Reader::~Reader()                                   = default;
Reader::Reader(Reader &&) noexcept                  = default;
Reader &Reader::operator=(Reader &&) noexcept       = default;

std::expected<Reader, Error> Reader::open(const std::filesystem::path &path) {
    Reader r;
    r.impl_ = std::make_unique<Impl>();
    if (!r.impl_->load(path)) {
        return std::unexpected(Error::OpenFailed);
    }
    return r;
}

SectionId Reader::first_section() const {
    return SectionId(static_cast<long long>(impl_->header.first_section));
}

SectionId Reader::last_section() const {
    return SectionId(static_cast<long long>(impl_->header.last_section));
}

std::size_t Reader::count() const {
    return static_cast<std::size_t>(impl_->header.last_section - impl_->header.first_section + 1);
}

PackId Reader::id() const {
    return impl_->header.pack_id;
}

std::optional<std::string> Reader::read(const SectionId &id) const {
    std::uint64_t raw = section_to_u64(id);
    if (raw < impl_->header.first_section || raw > impl_->header.last_section) {
        return std::nullopt;
    }
    auto frame_idx = impl_->find_frame(raw);
    if (frame_idx < 0) return std::nullopt;
    auto frame = impl_->decompress_frame(static_cast<std::size_t>(frame_idx));
    if (!frame.has_value()) return std::nullopt;
    const auto &fe = impl_->frame_index[frame_idx];
    return extract_section(*frame, raw, fe.first_section, fe.count);
}

std::vector<std::pair<SectionId, std::string>>
Reader::read_range(const SectionId &from, const SectionId &to) const {
    std::vector<std::pair<SectionId, std::string>> out;
    if (from > to) return out;

    std::uint64_t lo = section_to_u64(from);
    std::uint64_t hi = section_to_u64(to);
    lo               = std::max<std::uint64_t>(lo, impl_->header.first_section);
    hi               = std::min<std::uint64_t>(hi, impl_->header.last_section);
    if (lo > hi) return out;

    auto first_frame = impl_->find_frame(lo);
    auto last_frame  = impl_->find_frame(hi);
    if (first_frame < 0 || last_frame < 0) return out;

    for (std::ptrdiff_t fi = first_frame; fi <= last_frame; ++fi) {
        auto frame = impl_->decompress_frame(static_cast<std::size_t>(fi));
        if (!frame.has_value()) continue;
        const auto &fe = impl_->frame_index[fi];

        std::uint64_t frame_lo = std::max<std::uint64_t>(lo, fe.first_section);
        std::uint64_t frame_hi = std::min<std::uint64_t>(hi, fe.first_section + fe.count - 1);

        std::size_t pos = 0;
        for (std::uint64_t sid = fe.first_section;
             sid < fe.first_section + fe.count && pos < frame->size();
             ++sid) {
            if (pos + 4 > frame->size()) break;
            std::uint32_t len;
            std::memcpy(&len, frame->data() + pos, 4);
            pos += 4;
            if (pos + len > frame->size()) break;
            if (sid >= frame_lo && sid <= frame_hi) {
                out.emplace_back(SectionId(static_cast<long long>(sid)),
                                 frame->substr(pos, len));
            }
            pos += len;
        }
    }
    return out;
}

std::expected<void, Error>
write(const std::filesystem::path            &path,
      PackId                                   pack_id,
      const std::map<SectionId, std::string> &sections) {
    if (sections.empty()) return std::unexpected(Error::EmptyInput);

    SectionId first, last;
    if (!verify_consecutive(sections, first, last)) {
        return std::unexpected(Error::NonConsecutiveSections);
    }

    std::uint64_t first_int, last_int;
    try {
        first_int = section_to_u64(first);
        last_int  = section_to_u64(last);
    } catch (...) {
        return std::unexpected(Error::InvalidFormat);
    }

    std::string          dict = build_dict(sections);
    Compression::Context ctx(dict, COMPRESSION_LEVEL);

    std::vector<FrameEntry> frame_index;
    std::string             data_blob;

    auto it = sections.begin();
    while (it != sections.end()) {
        std::string raw_frame;
        FrameEntry  fe{};
        fe.first_section = section_to_u64(it->first);
        fe.offset        = data_blob.size();

        std::uint32_t count = 0;
        while (it != sections.end() && count < SECTIONS_PER_FRAME) {
            const std::string &payload = it->second;
            std::uint32_t      len     = static_cast<std::uint32_t>(payload.size());
            raw_frame.append(reinterpret_cast<const char *>(&len), 4);
            raw_frame.append(payload);
            ++count;
            ++it;
        }
        fe.count = count;

        auto compressed = ctx.compress_frame(raw_frame);
        if (!compressed.has_value()) {
            return std::unexpected(Error::CompressionFailed);
        }
        fe.size = static_cast<std::uint32_t>(compressed->size());
        data_blob.append(*compressed);

        frame_index.push_back(fe);
    }

    Header hdr{};
    hdr.magic         = MAGIC;
    hdr.version       = FORMAT_VERSION;
    hdr.pack_id       = pack_id;
    hdr.first_section = first_int;
    hdr.last_section  = last_int;
    hdr.frame_count   = static_cast<std::uint32_t>(frame_index.size());

    std::uint64_t cursor = sizeof(Header);
    hdr.dict_offset      = cursor;
    hdr.dict_size        = dict.size();
    cursor += hdr.dict_size;

    hdr.data_offset = cursor;
    hdr.data_size   = data_blob.size();
    cursor += hdr.data_size;

    hdr.frame_index_offset = cursor;
    hdr.frame_index_size   = frame_index.size() * sizeof(FrameEntry);
    cursor += hdr.frame_index_size;

    std::string out;
    out.reserve(cursor + FOOTER_SIZE);
    out.append(reinterpret_cast<const char *>(&hdr), sizeof(Header));
    out.append(dict);
    out.append(data_blob);
    if (!frame_index.empty()) {
        out.append(reinterpret_cast<const char *>(frame_index.data()),
                   frame_index.size() * sizeof(FrameEntry));
    }

    std::string checksum = compute_checksum(out.data(), out.size());
    out.append(checksum);
    std::uint32_t footer_magic = MAGIC;
    out.append(reinterpret_cast<const char *>(&footer_magic), 4);

    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return std::unexpected(Error::WriteFailed);
        f.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!f) return std::unexpected(Error::WriteFailed);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::unexpected(Error::WriteFailed);
    }

    return {};
}

} // namespace Pack
