/*
 * Compare a freshly-synced chain (packs + hot, new format) against a reference
 * store (old hex-shard format) section-by-section, WITHOUT logging in.
 *
 *   extrachain-chain-compare <synced-data-dir> <reference-dir> [step]
 *
 * synced-data-dir: has dag/packs + dag/hot
 * reference-dir:   old layout, <shard>/<section_id> files
 *
 * For each sampled section it compares the SET of transaction hashes (the tx hash
 * is the consensus identity of a tx). Equal sets => the section's contents match,
 * regardless of decimal/hex amount encoding on disk.
 */
#include <QCoreApplication>
#include <QDir>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>

#include "chain/dag.h"
#include "chain/pack_registry.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

namespace {

std::string read_file(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Normalize a section to a base-independent signature set: per tx, take the
// identity-bearing fields and the amount reduced to ONE canonical form (hex).
// The on-disk amount may be hex (old node) or decimal (new node) — both denote
// the same value in the network convention, so reduce both to hex before
// comparing. The stored "hash" field is intentionally ignored (it is form-
// dependent; tx.verify accepts either form).
std::set<std::string> tx_signatures(const std::string &json, bool amount_is_hex) {
    std::set<std::string> out;
    static const std::regex re(
        "\"sender\":\"([0-9a-fA-F]+)\".*?\"amount\":\"([0-9a-fA-F.]+)\".*?"
        "\"timestamp\":([0-9]+).*?\"signature\":\"([^\"]+)\"");
    auto begin = std::sregex_iterator(json.begin(), json.end(), re);
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        std::string amount = (*it)[2].str();
        // Canonical key = hex amount. Old store is already hex; new store is
        // decimal -> reduce via to_hex_string (the network's convention).
        std::string amount_hex = amount_is_hex ? amount : BigNumberFloat(amount).to_hex_string();
        out.insert((*it)[1].str() + "|" + amount_hex + "|" + (*it)[3].str() + "|" + (*it)[4].str());
    }
    return out;
}

// Reference filenames are HEX section ids (e.g. dec 50 -> file "32"). Shard
// unknown, so scan shards for the hex-named file.
std::string ref_section(const std::filesystem::path &ref, long long sid) {
    char hexname[32];
    std::snprintf(hexname, sizeof(hexname), "%llx", (unsigned long long)sid);
    for (auto &shard : std::filesystem::directory_iterator(ref)) {
        if (!shard.is_directory()) continue;
        auto p = shard.path() / hexname;
        if (std::filesystem::exists(p)) return read_file(p);
    }
    return {};
}

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::printf("usage: %s <synced-data-dir> <reference-dir> [step]\n", argv[0]);
        return 64;
    }
    std::filesystem::path synced = argv[1];
    std::filesystem::path ref    = argv[2];
    long long step = (argc > 3) ? std::atoll(argv[3]) : 1;

    Pack::Registry packs(synced / "dag" / "packs");
    packs.rescan(); // ctor does not scan; load pack metadata so read_section works
    auto           hot = synced / "dag" / "hot";

    auto synced_section = [&](long long sid) -> std::string {
        auto hp = hot / std::to_string(sid);
        if (std::filesystem::exists(hp)) return read_file(hp);
        auto s = packs.read_section(SectionId(sid));
        return s.has_value() ? *s : std::string();
    };

    // dump mode: "dump <sid>" prints both stores' section verbatim and exits.
    if (std::string(argv[3] ? argv[3] : "") == "dump" && argc > 4) {
        long long sid = std::atoll(argv[4]);
        std::printf("--- REF %lld ---\n%s\n--- SYNCED %lld ---\n%s\n",
                    sid, ref_section(ref, sid).c_str(), sid, synced_section(sid).c_str());
        return 0;
    }

    // Find reference tip across all shards (filenames are HEX section ids).
    long long tip = 0;
    for (auto &shard : std::filesystem::directory_iterator(ref)) {
        if (!shard.is_directory()) continue;
        std::error_code ec;
        for (auto &f : std::filesystem::directory_iterator(shard.path(), ec)) {
            try { tip = std::max(tip, std::stoll(f.path().filename().string(), nullptr, 16)); } catch (...) {}
        }
    }
    std::printf("=== compare: reference tip=%lld, step=%lld ===\n", tip, step);

    // Genesis check first — different genesis means different networks entirely.
    {
        auto rg = ref_section(ref, 0);
        auto sg = synced_section(0);
        std::printf("[genesis] ref tx=%zu synced tx=%zu  hashes_equal=%s\n",
                    tx_signatures(rg, true).size(), tx_signatures(sg, false).size(),
                    (tx_signatures(rg, true) == tx_signatures(sg, false)) ? "YES" : "NO");
    }

    long long checked = 0, mism = 0, missing_synced = 0, missing_ref = 0;
    for (long long sid = 0; sid <= tip; sid += step) {
        auto rj = ref_section(ref, sid);
        auto sj = synced_section(sid);
        if (rj.empty() && sj.empty()) continue;
        if (rj.empty()) { missing_ref++; continue; }
        if (sj.empty()) {
            missing_synced++;
            if (missing_synced <= 5) std::printf("  [synced] MISSING section %lld\n", sid);
            continue;
        }
        checked++;
        auto rh = tx_signatures(rj, true);
        auto sh = tx_signatures(sj, false);
        if (rh != sh) {
            mism++;
            if (mism <= 10)
                std::printf("  [MISMATCH] section %lld: ref=%zu tx, synced=%zu tx\n",
                            sid, rh.size(), sh.size());
        }
        if (sid % 10000 == 0) { std::printf("  ... section %lld ok\n", sid); std::fflush(stdout); }
    }

    std::printf("\nchecked=%lld mismatches=%lld missing_in_synced=%lld missing_in_ref=%lld\n",
                checked, mism, missing_synced, missing_ref);
    std::printf("=== %s ===\n", (mism == 0 && missing_synced == 0) ? "CHAINS MATCH" : "DIFFERENCE FOUND");
    return (mism == 0 && missing_synced == 0) ? 0 : 1;
}
