// How much does a vector row cost as the vector grows? add_vector_row recomputes
// the vector's content hash (DbConnector::hash_size, a full table scan) after every
// row, so the per-row cost is expected to grow linearly with the row count.
// Not a ctest: run by hand, EXC_BENCH_ROWS sets the total (default 20000).

#include <chrono>
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
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-dfs-vector-bench-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    const char *rows_env   = std::getenv("EXC_BENCH_ROWS");
    const auto  total_rows = rows_env ? std::strtoull(rows_env, nullptr, 10) : 20000ULL;

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

    const std::string payload(120, 'm'); // a chat-message-sized row
    auto              window_start = std::chrono::steady_clock::now();
    const auto        bench_start  = window_start;
    std::printf("rows,ms_per_row_in_window,ms_hash_size_now\n");
    for (std::uint64_t index = 1; index <= total_rows; ++index) {
        DbRow entry;
        entry["id"]       = "row_" + std::to_string(index);
        entry["payload"]  = payload;
        entry["position"] = std::to_string(index);
        TEST_REQUIRE(node->dfs()->add_vector_row(owner_id, vector->file_id, entry));
        if (index % 1000 == 0 || index == total_rows) {
            const auto now    = std::chrono::steady_clock::now();
            const auto window = std::chrono::duration<double, std::milli>(now - window_start).count();
            // Time one hash_size on its own at this size.
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
                        window / 1000.0,
                        std::chrono::duration<double, std::milli>(h1 - h0).count());
            std::fflush(stdout);
            window_start = std::chrono::steady_clock::now();
        }
    }
    const auto total = std::chrono::duration<double>(std::chrono::steady_clock::now() - bench_start).count();
    std::printf("total %llu rows in %.1f s\n", static_cast<unsigned long long>(total_rows), total);

    node->cleanUp();
    node.reset();
    std::error_code ignored;
    std::filesystem::current_path(std::filesystem::temp_directory_path(), ignored);
    std::filesystem::remove_all(test_path, ignored);
    return 0;
}
