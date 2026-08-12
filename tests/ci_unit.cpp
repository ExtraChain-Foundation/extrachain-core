// Tiny unit check for ControlIndex put/get/last/erase, no full node.
#include <QCoreApplication>
#include <QDir>
#include <cstdio>
#include <filesystem>

#include "chain/control_index.h"
#include "utils/exc_utils_base64.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    std::filesystem::remove_all("/tmp/ci-unit");
    std::filesystem::create_directories("/tmp/ci-unit");
    QDir::setCurrent("/tmp/ci-unit");

    ControlIndex ci(nullptr);
    std::printf("initial rows=%llu\n", (unsigned long long)ci.row_count());

    ci.put(SectionId(20), "hash20");
    ci.put(SectionId(40), "hash40");
    ci.put(SectionId(60), "hash60");
    std::printf("after 3 puts rows=%llu\n", (unsigned long long)ci.row_count());

    auto g = ci.get(SectionId(40));
    std::printf("get(40)=%s\n", g.has_value() ? g.value().c_str() : "MISSING");

    auto l = ci.last_at_or_below(SectionId(55));
    std::printf("last<=55: %s -> %s\n",
                l.has_value() ? l.value().first.to_string().c_str() : "?",
                l.has_value() ? l.value().second.c_str() : "MISSING");

    auto top = ci.last_at_or_below(SectionId(-1));
    std::printf("last<=top: %s -> %s\n",
                top.has_value() ? top.value().first.to_string().c_str() : "?",
                top.has_value() ? top.value().second.c_str() : "MISSING");

    ci.erase(SectionId(40));
    std::printf("after erase(40) get(40)=%s rows=%llu\n",
                ci.get(SectionId(40)).has_value() ? "present" : "gone",
                (unsigned long long)ci.row_count());

    // overwrite
    ci.put(SectionId(20), "hash20b");
    std::printf("overwrite get(20)=%s\n", ci.get(SectionId(20)).value_or("?").c_str());

    // --- edge cases ---
    int  pass = 0, fail = 0;
    auto check = [&](const char *name, bool ok) {
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
        ok ? pass++ : fail++;
    };

    const std::string base64_binary("\0\x01\xfb\xff", 4);
    const auto        base64_encoded = Utils::to_base64(base64_binary);
    const auto        base64_decoded = Utils::from_base64(base64_encoded);
    check("base64 URL-safe encoding", base64_encoded == "AAH7_w");
    check("base64 URL-safe round-trip", base64_decoded.has_value() && base64_decoded.value() == base64_binary);
    const auto standard_base64 = Utils::from_base64("AAH7/w==");
    check("base64 standard alphabet compatibility",
          standard_base64.has_value() && standard_base64.value() == base64_binary);
    const auto empty_base64 = Utils::from_base64("");
    check("base64 empty value round-trip", empty_base64.has_value() && empty_base64.value().empty());
    check("base64 invalid padding rejected", !Utils::from_base64("A===").has_value());

    // state now: {20:hash20b, 60:hash60} (40 erased)
    check("get missing -> nullopt", !ci.get(SectionId(99)).has_value());
    check("last<=below-all -> nullopt", !ci.last_at_or_below(SectionId(5)).has_value());
    check("last<=exact boundary 60", ci.last_at_or_below(SectionId(60)).value().first == SectionId(60));
    check("last<=between 20 and 60 -> 20", ci.last_at_or_below(SectionId(59)).value().first == SectionId(20));
    check("last<=above-all -> top 60", ci.last_at_or_below(SectionId(1000)).value().first == SectionId(60));
    check("erased 40 skipped: last<=50 -> 20", ci.last_at_or_below(SectionId(50)).value().first == SectionId(20));
    check("erase non-existent is safe", (ci.erase(SectionId(12345)), true));
    ci.put(SectionId(0), "genesis");
    check("section 0 stored", ci.get(SectionId(0)).value_or("") == "genesis");
    check("last<=0 -> 0", ci.last_at_or_below(SectionId(0)).value().first == SectionId(0));
    ci.clear();
    check("clear empties", ci.row_count() == 0);
    check("get after clear -> nullopt", !ci.get(SectionId(20)).has_value());

    std::printf("EDGE: %d pass, %d fail\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
