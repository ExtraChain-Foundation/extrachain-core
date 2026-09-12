#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "test_support.h"

#include <thread>

using namespace std::chrono_literals;

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-ingress-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("vector-ingress", ActorType::User, owner);
    auto schema = Dfs::CollectionTemplate::create("ingress").value();
    schema.use_id().add_fields({ Dfs::Field::String("payload").not_null() });
    const auto stored = node->dfs()->store_template(owner.id(), schema).value();
    const auto file =
        node->dfs()->store_vector(owner.id(), owner.id(), "ingress", owner.id(), stored.file_id).value();
    auto vector   = node->dfs()->make_vector(owner.id(), file.file_id).value().second;
    auto make_row = [&](unsigned index) {
        DbRow row { { "id", std::to_string(index) },
                    { "payload", "value" },
                    { "actor", owner.id().to_string() },
                    { "status", "1" },
                    { "timestamp", std::to_string(Utils::current_date_ms()) } };
        row["sign"] = ByteArray(owner.key().sign(vector.calculate_hash(row).first).value()).toString();
        return row;
    };
    const auto sample = make_row(0);
    for (const auto& field : { "actor", "sign", "timestamp", "status" }) {
        auto row = sample;
        row.erase(field);
        TEST_REQUIRE(!node->dfs()->network_vector_add(owner.id(), file.file_id, row));
    }
    for (const auto& timestamp : { "", "-1", "1x", "18446744073709551616" }) {
        auto row         = sample;
        row["timestamp"] = timestamp;
        TEST_REQUIRE(!node->dfs()->network_vector_add(owner.id(), file.file_id, row));
    }
    auto oversized       = sample;
    oversized["payload"] = std::string(1024 * 1024, 'x');
    TEST_REQUIRE(!node->dfs()->network_vector_add(owner.id(), file.file_id, oversized));

    DbConnector blocker(Dfs::Path::file_path(owner.id(), file.file_id).value());
    TEST_REQUIRE(blocker.open(false) && blocker.query("BEGIN IMMEDIATE"));
    std::atomic_uint   accepted { 0 };
    std::vector<DbRow> rows;
    for (unsigned peer = 0; peer < 4; ++peer) {
        for (unsigned index = 0; index < 8; ++index) {
            rows.push_back(make_row(peer * 8 + index));
            TEST_REQUIRE(node->dfs()->network_vector_add(owner.id(),
                                                         file.file_id,
                                                         rows.back(),
                                                         std::string(64, char('a' + peer)),
                                                         [&] {
                                                             ++accepted;
                                                         }));
        }
        TEST_REQUIRE(
            !node->dfs()->network_vector_add(owner.id(), file.file_id, sample, std::string(64, char('a' + peer))));
    }
    TEST_REQUIRE(!node->dfs()->network_vector_add(owner.id(), file.file_id, sample, std::string(64, 'e')));
    TEST_REQUIRE_EQ(accepted.load(), 0u);
    TEST_REQUIRE(blocker.query("ROLLBACK"));
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (accepted < 32 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    TEST_REQUIRE_EQ(accepted.load(), 32u);
    TEST_REQUIRE_EQ(vector.index_root().value().tree.rows, std::uint64_t(32));
    const auto replay = vector.local_add(rows.front(), true);
    TEST_REQUIRE(replay.has_value() && !replay.value());
    auto forged       = rows.front();
    forged["payload"] = "forged";
    TEST_REQUIRE(!vector.local_add(forged, true).has_value());
    const auto next = make_row(32);
    TEST_REQUIRE(node->dfs()->network_vector_add(owner.id(), file.file_id, next, std::string(64, 'a'), [&] {
        ++accepted;
    }));
    const auto next_deadline = std::chrono::steady_clock::now() + 5s;
    while (accepted < 33 && std::chrono::steady_clock::now() < next_deadline)
        std::this_thread::sleep_for(10ms);
    TEST_REQUIRE_EQ(accepted.load(), 33u);
    node->dfs()->prepare_shutdown();
    TEST_REQUIRE(!node->dfs()->network_vector_add(owner.id(), file.file_id, sample));
    TEST_REQUIRE(blocker.close());
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
