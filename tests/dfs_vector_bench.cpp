// Measure vector insertion and persistent root lookup at increasing sizes.
// Manual benchmark: EXC_BENCH_ROWS defaults to 20000; EXC_BENCH_PAYLOAD_BYTES defaults to 120.

#include <chrono>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "chain/actor.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/vector_index.h"
#include "dfs/dfs_utils.h"
#include "dfs/dirs_manager.h"
#include "managers/account_controller.h"
#include "test_support.h"
#include "utils/db_connector.h"
#include "utils/exc_utils.h"

int main() {
    const auto parameter = [](const char* name, std::uint64_t fallback, std::uint64_t maximum) {
        const char* raw = std::getenv(name);
        if (raw == nullptr)
            return fallback;
        const std::string_view text(raw);
        std::uint64_t          value  = 0;
        const auto             parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        TEST_REQUIRE(parsed.ec == std::errc { } && parsed.ptr == text.data() + text.size() && value != 0
                     && value <= maximum);
        return value;
    };
    const auto total_rows    = parameter("EXC_BENCH_ROWS", 20000, 1000000);
    const auto payload_bytes = parameter("EXC_BENCH_PAYLOAD_BYTES", 120, 65536);
    TEST_REQUIRE(total_rows * payload_bytes <= 1024ULL * 1024 * 1024);
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-dfs-vector-bench-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("bench-profile", ActorType::User, owner);
    const auto owner_id = owner.id();

    auto collection_template = Dfs::CollectionTemplate::create("bench_vector");
    TEST_REQUIRE(collection_template.has_value());
    auto vector_template = collection_template.value().use_id().add_fields(
        { Dfs::Field::String("payload").not_null(), Dfs::Field::Integer("position").not_null() });
    const auto stored_template = node->dfs()->store_template(owner_id, vector_template);
    TEST_REQUIRE(stored_template.has_value());
    const auto vector =
        node->dfs()->store_vector(owner_id, owner_id, "bench_vector", owner_id, stored_template->file_id);
    TEST_REQUIRE(vector.has_value());

    const std::string payload(payload_bytes, 'm');
    std::uint64_t     last_window_row = 0;
    auto              window_start = std::chrono::steady_clock::now();
    const auto        bench_start  = window_start;
    std::printf("payload_bytes=%llu\n", static_cast<unsigned long long>(payload_bytes));
    std::printf("rows,ms_per_row_in_window,ms_root_lookup_now\n");
    for (std::uint64_t index = 1; index <= total_rows; ++index) {
        DbRow entry;
        entry["id"]       = "row_" + std::to_string(index);
        entry["payload"]  = payload;
        entry["position"] = std::to_string(index);
        TEST_REQUIRE(node->dfs()->add_vector_row(owner_id, vector->file_id, entry));
        if (index % 1000 == 0 || index == total_rows) {
            const auto now    = std::chrono::steady_clock::now();
            const auto window = std::chrono::duration<double, std::milli>(now - window_start).count();
            const auto path = Dfs::Path::file_path(owner_id, vector->file_id);
            TEST_REQUIRE(path.has_value());
            DbConnector db(path->native());
            TEST_REQUIRE(db.open(false));
            const auto       h0 = std::chrono::steady_clock::now();
            Dfs::VectorIndex vector_index(db, "id");
            TEST_REQUIRE(vector_index.root().has_value());
            const auto h1 = std::chrono::steady_clock::now();
            std::printf("%llu,%.3f,%.1f\n",
                        static_cast<unsigned long long>(index),
                        window / static_cast<double>(index - last_window_row),
                        std::chrono::duration<double, std::milli>(h1 - h0).count());
            std::fflush(stdout);
            window_start = std::chrono::steady_clock::now();
            last_window_row = index;
        }
    }
    const auto total = std::chrono::duration<double>(std::chrono::steady_clock::now() - bench_start).count();
    std::printf("total %llu rows in %.1f s\n", static_cast<unsigned long long>(total_rows), total);

    const auto  path = Dfs::Path::file_path(owner_id, vector->file_id).value();
    std::string expected_root;
    {
        DbConnector db(path);
        TEST_REQUIRE(db.open(false));
        const auto totals = db.select("SELECT COUNT(*) AS rows, SUM(length(payload)) AS bytes FROM Vector");
        TEST_REQUIRE_EQ(totals.size(), 1);
        TEST_REQUIRE_EQ(totals.front().at("rows"), std::to_string(total_rows));
        TEST_REQUIRE_EQ(totals.front().at("bytes"), std::to_string(total_rows * payload_bytes));
        Dfs::VectorIndex index(db, "id");
        expected_root = index.root().value().hash;
        std::printf("verified_rows=%llu logical_payload_bytes=%llu\n",
                    static_cast<unsigned long long>(total_rows),
                    static_cast<unsigned long long>(total_rows * payload_bytes));
    }

    node->cleanUp();
    node.reset();
    {
        DbConnector db(path);
        TEST_REQUIRE(db.open(false));
        Dfs::VectorIndex index(db, "id");
        TEST_REQUIRE_EQ(index.root().value().hash, expected_root);
        std::printf("reopened_root=%s\n", expected_root.c_str());
    }
    std::error_code ignored;
    std::filesystem::current_path(std::filesystem::temp_directory_path(), ignored);
    std::filesystem::remove_all(test_path, ignored);
    return 0;
}
