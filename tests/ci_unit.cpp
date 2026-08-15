// Tiny unit check for ControlIndex put/get/last/erase, no full node.
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "chain/control_index.h"
#include "utils/db_connector.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"
#include "utils/file_io.h"
#include "utils/legacy_compression.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const auto process_path = std::filesystem::current_path();
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-ci-unit-" + Utils::generate_random_hex(8));
    const auto clean_state_path = test_path / "clean-state";
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    auto ci = std::make_unique<ControlIndex>(nullptr);
    std::printf("initial rows=%llu\n", (unsigned long long)ci->row_count());

    ci->put(SectionId(20), "hash20");
    ci->put(SectionId(40), "hash40");
    ci->put(SectionId(60), "hash60");
    std::printf("after 3 puts rows=%llu\n", (unsigned long long)ci->row_count());

    auto g = ci->get(SectionId(40));
    std::printf("get(40)=%s\n", g.has_value() ? g.value().c_str() : "MISSING");

    auto l = ci->last_at_or_below(SectionId(55));
    std::printf("last<=55: %s -> %s\n",
                l.has_value() ? l.value().first.to_string().c_str() : "?",
                l.has_value() ? l.value().second.c_str() : "MISSING");

    auto top = ci->last_at_or_below(SectionId(-1));
    std::printf("last<=top: %s -> %s\n",
                top.has_value() ? top.value().first.to_string().c_str() : "?",
                top.has_value() ? top.value().second.c_str() : "MISSING");

    ci->erase(SectionId(40));
    std::printf("after erase(40) get(40)=%s rows=%llu\n",
                ci->get(SectionId(40)).has_value() ? "present" : "gone",
                (unsigned long long)ci->row_count());

    // overwrite
    ci->put(SectionId(20), "hash20b");
    std::printf("overwrite get(20)=%s\n", ci->get(SectionId(20)).value_or("?").c_str());

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
    check("file name whitespace normalization", Utils::fix_file_name("  alpha   beta  ") == "alpha beta");
    check("file name invalid run replacement", Utils::fix_file_name("a+%b") == "a_b");
    check("file name quote replacement", Utils::fix_file_name("a«b»c") == "a_b_c");

    ExtraChainSettings audit_settings;
    audit_settings.dag_audit_cursor = "1240";
    const auto restored_settings    = Json::deserialize<ExtraChainSettings>(Json::serialize(audit_settings));
    check("DAG audit cursor settings round-trip",
          restored_settings.has_value() && restored_settings.value().dag_audit_cursor == "1240");

    constexpr std::string_view legacy_payload = "ExtraChain legacy compression";
    const auto                 compressed     = LegacyCompression::compress(legacy_payload);
    const std::string          expected_compressed(
        "\x00\x00\x00\x1d\x78\x9c\x73\xad\x28\x29\x4a\x74\xce\x48\xcc\xcc\x53\xc8\x49\x4d\x4f"
                 "\x4c\xae\x54\x48\xce\xcf\x2d\x28\x4a\x2d\x2e\xce\xcc\xcf\x03\x00\xa5\x57\x0b\x4f",
        41);
    check("legacy compression matches Qt wire format",
          compressed.has_value() && compressed.value() == expected_compressed);
    const auto decompressed = compressed.has_value()
                                  ? LegacyCompression::decompress(compressed.value(), 1024)
                                  : std::expected<std::string, LegacyCompression::Error>(
                                        std::unexpected(LegacyCompression::Error::CompressFailed));
    check("legacy compression round-trip", decompressed.has_value() && decompressed.value() == legacy_payload);
    check("legacy compression size limit",
          compressed.has_value() && !LegacyCompression::decompress(compressed.value(), 4).has_value());
    const auto compressed_empty   = LegacyCompression::compress({});
    const auto decompressed_empty = compressed_empty.has_value()
                                        ? LegacyCompression::decompress(compressed_empty.value(), 0)
                                        : std::expected<std::string, LegacyCompression::Error>(
                                              std::unexpected(LegacyCompression::Error::CompressFailed));
    check("legacy empty compression round-trip", decompressed_empty.has_value() && decompressed_empty->empty());

    const auto atomic_path = test_path / "atomic-state";
    check("atomic file write", FileIo::write_atomic(atomic_path, "first").has_value());
    check("atomic file replace", FileIo::write_atomic(atomic_path, "second").has_value());
    const auto atomic_data = FileIo::read_all(atomic_path);
    check("atomic file read", atomic_data.has_value() && atomic_data.value() == "second");

    const auto  database_path = test_path / "concurrent.sqlite";
    DbConnector first_database(database_path);
    DbConnector second_database(database_path);
    check("open first concurrent database", first_database.open());
    check("create concurrent table",
          first_database.query("CREATE TABLE concurrent_rows (row_id TEXT PRIMARY KEY, payload TEXT NOT NULL)"));
    check("open second concurrent database", second_database.open());
    std::vector<std::jthread> database_workers;
    for (std::size_t worker = 0; worker < 4; ++worker) {
        database_workers.emplace_back([&, worker] {
            auto &database = worker % 2 == 0 ? first_database : second_database;
            for (std::size_t row = 0; row < 100; ++row) {
                const auto key = std::to_string(worker) + '-' + std::to_string(row);
                database.insert("concurrent_rows", { { "row_id", key }, { "payload", "stored" } });
            }
        });
    }
    database_workers.clear();
    check("concurrent SQLite connections retain all rows", first_database.count("concurrent_rows") == 400);
    check("close second concurrent database", second_database.close());
    check("close first concurrent database", first_database.close());

    // state now: {20:hash20b, 60:hash60} (40 erased)
    check("get missing -> nullopt", !ci->get(SectionId(99)).has_value());
    check("last<=below-all -> nullopt", !ci->last_at_or_below(SectionId(5)).has_value());
    check("last<=exact boundary 60", ci->last_at_or_below(SectionId(60)).value().first == SectionId(60));
    check("last<=between 20 and 60 -> 20", ci->last_at_or_below(SectionId(59)).value().first == SectionId(20));
    check("last<=above-all -> top 60", ci->last_at_or_below(SectionId(1000)).value().first == SectionId(60));
    check("count through 40", ci->row_count_at_or_below(SectionId(40)) == 1);
    check("count through 60", ci->row_count_at_or_below(SectionId(60)) == 2);
    check("erased 40 skipped: last<=50 -> 20", ci->last_at_or_below(SectionId(50)).value().first == SectionId(20));
    check("erase non-existent is safe", (ci->erase(SectionId(12345)), true));
    ci->put(SectionId(0), "genesis");
    check("section 0 stored", ci->get(SectionId(0)).value_or("") == "genesis");
    check("last<=0 -> 0", ci->last_at_or_below(SectionId(0)).value().first == SectionId(0));
    ci->clear();
    check("clear empties", ci->row_count() == 0);
    check("get after clear -> nullopt", !ci->get(SectionId(20)).has_value());

    ci.reset();
    std::filesystem::current_path(process_path);
    std::filesystem::create_directories(clean_state_path);
    std::filesystem::current_path(clean_state_path);
    {
        ControlIndex first_open(nullptr);
        check("new index requires rebuild", first_open.rebuild_required());
    }
    {
        ControlIndex clean_reopen(nullptr);
        check("cleanly closed index skips rebuild", !clean_reopen.rebuild_required());
    }
    std::filesystem::current_path(process_path);
    std::filesystem::remove_all(test_path);

    std::printf("EDGE: %d pass, %d fail\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
