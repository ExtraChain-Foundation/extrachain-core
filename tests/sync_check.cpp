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

// Real pack-sync transfer over a generated data set, without sockets.
//
//   ./extrachain-sync-check <server-data-dir> [client-dir]
//
// The server dir is a node working directory produced by extrachain-gen-sections
// (it contains dag/packs). This drives the exact code the network uses:
//   server: Registry::spans()  -> what network_pack_list_request answers
//           Registry::read_raw -> what network_pack_request ships
//   client: Registry::install_raw -> what network_pack_data_response stores
// then verifies every packed section reads back byte-identically on the client.

#include <QByteArray>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "chain/dag.h" // SectionFileData / FileSectionsSync
#include "chain/hot_section_store.h"
#include "chain/pack.h"
#include "chain/pack_registry.h"
#include "utils/bignumber.h"
#include "utils/exc_utils.h" // MessagePack

namespace {

    std::string read_file(const std::filesystem::path &p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), {});
    }

    // Verify the hot-tail sync transform. Read the current SQLite store or legacy
    // loose files, apply the real wire transform, and write one client-side batch.
    std::uint64_t verify_hot_tail(const std::filesystem::path &server_hot,
                                  const std::filesystem::path &client_hot,
                                  std::uint64_t               &checked) {
        std::error_code ec;
        std::filesystem::create_directories(client_hot, ec);

        FileSectionsSync sync;
        const auto       hot_db = server_hot / "HotSections.db";
        if (std::filesystem::exists(hot_db, ec)) {
            HotSectionStore store(hot_db);
            if (const auto bounds = store.bounds(); bounds.has_value()) {
                for (auto &[section, payload] : store.read_range(bounds->first, bounds->second)) {
                    sync.sections.push_back(
                        SectionFileData { .section_id = section, .file_bytes = std::move(payload) });
                }
            }
        } else {
            for (auto &e : std::filesystem::directory_iterator(server_hot, ec)) {
                if (!e.is_regular_file())
                    continue;
                const auto section = SectionId::create(e.path().filename().string());
                if (!section.has_value())
                    continue;
                sync.sections.push_back(
                    SectionFileData { .section_id = *section, .file_bytes = read_file(e.path()) });
            }
        }

        // Real wire transform: serialize -> compress -> (network) -> decompress -> deserialize.
        auto ser        = MessagePack::serialize(sync);
        auto compressed = qCompress(QByteArray::fromStdString(ser));
        auto restored   = qUncompress(compressed);
        auto back       = MessagePack::deserialize<FileSectionsSync>(restored.toStdString());

        std::uint64_t mism = 0;
        if (!back.has_value()) {
            std::printf("[SyncCheck] hot tail: deserialize FAILED\n");
            return sync.sections.size();
        }

        std::map<SectionId, std::string> restored_sections;
        for (const auto &sfd : back->sections) {
            restored_sections.insert_or_assign(sfd.section_id, sfd.file_bytes);
            ++checked;
        }
        HotSectionStore client(client_hot / "HotSections.db");
        if (!restored_sections.empty() && !client.put_many(restored_sections))
            return restored_sections.size();
        for (const auto &[section, payload] : restored_sections) {
            if (client.get(section) != std::optional<std::string>(payload))
                ++mism;
        }
        return mism;
    }

} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::printf("usage: %s <server-data-dir> [client-dir]\n", argv[0]);
        return 64;
    }
    std::filesystem::path server_packs = std::filesystem::path(argv[1]) / "dag" / "packs";
    std::filesystem::path client_packs = (argc > 2)
                                             ? std::filesystem::path(argv[2])
                                             : std::filesystem::temp_directory_path() / "exc_synccheck_client";

    std::error_code ec;
    if (!std::filesystem::exists(server_packs, ec)) {
        std::printf("[SyncCheck] no packs at %s\n", server_packs.string().c_str());
        return 1;
    }
    std::filesystem::remove_all(client_packs, ec);

    Pack::Registry server(server_packs);
    server.rescan();
    Pack::Registry client(client_packs);

    auto spans = server.spans(); // network_pack_list_request payload
    std::printf("[SyncCheck] server has %zu packs\n", spans.size());
    if (spans.empty()) {
        std::printf("[SyncCheck] nothing to sync (no sealed packs)\n");
        return 1;
    }

    auto t0 = std::chrono::steady_clock::now();

    // Transfer + install each pack exactly as the network path does.
    std::size_t   installed   = 0;
    std::uint64_t bytes_moved = 0;
    for (const auto &s : spans) {
        auto raw = server.read_raw(s.id); // network_pack_request
        if (!raw.has_value()) {
            std::printf("[SyncCheck] FAIL: server read_raw(%llu) failed\n", static_cast<unsigned long long>(s.id));
            return 2;
        }
        bytes_moved += raw->size();
        auto inst = client.install_raw(s.id, *raw); // network_pack_data_response
        if (!inst.has_value()) {
            std::printf("[SyncCheck] FAIL: client install_raw(%llu) rejected (error %d)\n",
                        static_cast<unsigned long long>(s.id),
                        static_cast<int>(inst.error()));
            return 2;
        }
        ++installed;
    }

    // Verify: every packed section reads back identically on the client.
    auto cov = client.coverage();
    if (!cov.has_value()) {
        std::printf("[SyncCheck] FAIL: client coverage empty after install\n");
        return 2;
    }

    std::uint64_t first   = std::stoull(cov->first.to_string());
    std::uint64_t last    = std::stoull(cov->last.to_string());
    std::uint64_t checked = 0, mismatched = 0;
    for (std::uint64_t sid = first; sid <= last; ++sid) {
        auto srv = server.read_section(SectionId(static_cast<long long>(sid)));
        auto cli = client.read_section(SectionId(static_cast<long long>(sid)));
        if (!cli.has_value() || cli != srv) {
            if (mismatched < 5) {
                std::printf("[SyncCheck] mismatch at section %llu\n", static_cast<unsigned long long>(sid));
            }
            ++mismatched;
        }
        ++checked;
    }

    // Hot tail: the loose section files above the packed range are transferred by
    // file-sync, not packs. Verify that path too so the whole chain is covered.
    std::filesystem::path server_hot  = std::filesystem::path(argv[1]) / "dag" / "hot";
    std::uint64_t         hot_checked = 0, hot_mismatched = 0;
    if (std::filesystem::exists(server_hot, ec)) {
        hot_mismatched = verify_hot_tail(server_hot, client_packs.parent_path() / "hot", hot_checked);
        mismatched += hot_mismatched;
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    std::printf("[SyncCheck] installed %zu packs (%.1f MB), verified %llu packed sections [%llu..%llu]\n",
                installed,
                bytes_moved / 1048576.0,
                static_cast<unsigned long long>(checked),
                static_cast<unsigned long long>(first),
                static_cast<unsigned long long>(last));
    std::printf("[SyncCheck] hot tail: verified %llu sections, mismatches %llu\n",
                static_cast<unsigned long long>(hot_checked),
                static_cast<unsigned long long>(hot_mismatched));
    std::printf("[SyncCheck] total mismatches %llu, in %lld ms\n",
                static_cast<unsigned long long>(mismatched),
                static_cast<long long>(ms));
    std::printf("[SyncCheck] %s\n",
                mismatched == 0 ? "PASS — synced data matches the source" : "FAIL — synced data diverged");
    std::fflush(stdout);
    return mismatched == 0 ? 0 : 3;
}
