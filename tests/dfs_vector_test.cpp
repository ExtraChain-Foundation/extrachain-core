// Regression test for tracker #76: a vector row arriving from the network
// (DfsVectorAdd) used to reach DbRow::at() on fields the sender simply omitted —
// the primary field in calculate_hash, "timestamp" in network_vector_add — and
// std::out_of_range on the storage thread terminated the node. One malformed
// row from any peer was enough. Every such row must be rejected, not fatal, and
// a well-formed row must still land.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "chain/actor.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/dfs_utils.h"
#include "dfs/dirs_manager.h"
#include "managers/account_controller.h"
#include "test_support.h"
#include "utils/exc_utils.h"

namespace {
    // network_vector_add runs on the storage executor, which is a thread pool: a
    // barrier job proves the pool is alive, not that the job posted before it has
    // finished. Barrier, then settle, so a rejection is actually observed.
    void drain_storage(ExtraChain::Core::ExtraChainNode &node) {
        std::promise<void> done;
        auto               future = done.get_future();
        node.post_storage([&done] { done.set_value(); });
        TEST_REQUIRE(future.wait_for(std::chrono::seconds(30)) == std::future_status::ready);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
} // namespace

int main() {
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-dfs-vector-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);

    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("dfs-vector-profile", ActorType::User, owner);

    const auto owner_id = owner.id();
    auto       collection_template = Dfs::CollectionTemplate::create("hostile_rows");
    TEST_REQUIRE(collection_template.has_value());
    auto vector_template = collection_template.value().use_id().add_fields(
        { Dfs::Field::String("payload").not_null(), Dfs::Field::Integer("position").not_null() });
    const auto stored_template = node->dfs()->store_template(owner_id, vector_template);
    TEST_REQUIRE(stored_template.has_value());
    const auto vector = node->dfs()->store_vector(owner_id, owner_id, "hostile_rows", owner_id, stored_template->file_id);
    TEST_REQUIRE(vector.has_value());
    const auto file_id = vector->file_id;

    const auto row_count = [&]() -> std::size_t {
        const auto rows = node->dfs()->read_vector_rows(owner_id, file_id);
        return rows.has_value() ? rows->size() : 0;
    };

    // The catalog row of a vector must follow its content: the hash column is what
    // the catalog digest (#75) compares, so a vector that gained a row has to look
    // different from a copy that did not.
    auto      &dirs        = node->dfs_service()->dirs_manager();
    const auto catalog_row = [&]() {
        const auto row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs.get_db_instance(), owner_id, file_id);
        TEST_REQUIRE(row.has_value());
        return row.value();
    };
    const auto owner_digest = [&]() {
        const auto digests = dirs.catalog_digests({ owner_id });
        TEST_REQUIRE_EQ(digests.size(), std::size_t(1));
        return digests.front().digest;
    };
    const auto hash_empty   = catalog_row().hash;
    const auto digest_empty = owner_digest();

    // A genuine row through the local path: the baseline.
    DbRow genuine;
    genuine["id"]       = "row_0";
    genuine["payload"]  = "hello";
    genuine["position"] = "0";
    TEST_REQUIRE(node->dfs()->add_vector_row(owner_id, file_id, genuine));
    TEST_REQUIRE_EQ(row_count(), std::size_t(1));

    const auto hash_one   = catalog_row().hash;
    const auto digest_one = owner_digest();
    TEST_REQUIRE(hash_one != hash_empty);
    TEST_REQUIRE(digest_one != digest_empty);

    DbRow second;
    second["id"]       = "row_1";
    second["payload"]  = "world";
    second["position"] = "1";
    TEST_REQUIRE(node->dfs()->add_vector_row(owner_id, file_id, second));
    TEST_REQUIRE_EQ(row_count(), std::size_t(2));
    const auto hash_two   = catalog_row().hash;
    const auto digest_two = owner_digest();
    TEST_REQUIRE(hash_two != hash_one);
    TEST_REQUIRE(digest_two != digest_one);
    // The catalog row's own signature did not change: only hash (and date) moved.
    TEST_REQUIRE(catalog_row().sign == vector->sign);

    // A value that does not fit its column: rejected, not fatal. Before, the
    // INTEGER bind threw std::invalid_argument straight through add_vector_row.
    DbRow bad_number;
    bad_number["id"]       = "row_bad";
    bad_number["payload"]  = "x";
    bad_number["position"] = "abc";
    TEST_REQUIRE(!node->dfs()->add_vector_row(owner_id, file_id, bad_number));
    TEST_REQUIRE_EQ(row_count(), std::size_t(2));

    // Removal is a status flip that blanks the other fields; with an INTEGER
    // field that used to be "-" and the same fatal bind. Now the tombstone lands.
    TEST_REQUIRE(node->dfs()->remove_vector_row(owner_id, file_id, "row_1"));
    // read_vector_row hides tombstones (status = '1' only); look at the raw row.
    const auto removed = node->dfs()->read_vector_rows(owner_id, file_id, "WHERE id = 'row_1'");
    TEST_REQUIRE(removed.has_value());
    TEST_REQUIRE_EQ(removed->size(), std::size_t(1));
    TEST_REQUIRE_EQ(removed->front().at("status"), std::string("0"));
    TEST_REQUIRE_EQ(removed->front().at("position"), std::string("0"));
    TEST_REQUIRE(catalog_row().hash != hash_two);

    std::printf("vector catalog hash: empty=%s one=%s two=%s tombstone=%s\n",
                hash_empty.substr(0, 12).c_str(),
                hash_one.substr(0, 12).c_str(),
                hash_two.substr(0, 12).c_str(),
                catalog_row().hash.substr(0, 12).c_str());

    // A signed row as it looks on the wire, to be mutilated below.
    const auto stored = node->dfs()->read_vector_rows(owner_id, file_id);
    TEST_REQUIRE(stored.has_value() && !stored->empty());
    const DbRow wire = stored->front();
    TEST_REQUIRE(wire.contains("sign") && wire.contains("timestamp") && wire.contains("status")
                 && wire.contains("actor") && wire.contains("id"));

    const auto baseline = row_count();
    const auto hostile  = [&](const char *label, DbRow row) {
        node->dfs_service()->network_vector_add(owner_id, file_id, row);
        drain_storage(*node);
        // Rejected and the node is still here: the count is unchanged.
        TEST_REQUIRE_MESSAGE(row_count() == baseline, label);
    };

    DbRow no_primary = wire;
    no_primary.erase("id");
    hostile("row without the primary field", no_primary);

    DbRow no_timestamp = wire;
    no_timestamp.erase("timestamp");
    hostile("row without timestamp", no_timestamp);

    DbRow no_status = wire;
    no_status.erase("status");
    hostile("row without status", no_status);

    DbRow no_sign = wire;
    no_sign.erase("sign");
    hostile("row without signature", no_sign);

    DbRow no_actor = wire;
    no_actor.erase("actor");
    hostile("row without actor", no_actor);

    DbRow bad_timestamp = wire;
    bad_timestamp["timestamp"] = "yesterday";
    hostile("row with non-numeric timestamp", bad_timestamp);

    DbRow tampered = wire;
    tampered["payload"] = "forged";
    hostile("row with forged payload under the old signature", tampered);

    hostile("empty row", DbRow {});

    // A replay of the genuine wire row is not a crash either, and adds nothing new.
    hostile("replayed genuine row", wire);

    node->cleanUp();
    node.reset();
    std::error_code ignored;
    std::filesystem::current_path(std::filesystem::temp_directory_path(), ignored);
    std::filesystem::remove_all(test_path, ignored);
    std::printf("dfs vector hostile rows: PASS\n");
    return 0;
}
