#include <thread>

#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "dag_admission_fixture.h"
#include "network/network_runtime.h"
#include "test_support.h"

// A sync takes the pack lock before it fetches sections, and the network threads queue
// behind it. Sealing a range runs for minutes on a loaded host, so a pack in progress must
// give the lock back when a sync starts and seal the range afterwards.
int main() {
    using namespace std::chrono_literals;
    const auto original = std::filesystem::current_path();
    const auto home = std::filesystem::temp_directory_path() / ("extrachain-pack-yield-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->network_runtime().stop();
    node->process();
    auto* dag = node->dag();
    dag->set_mode(DagMode::Full);

    // Empty sections with valid control hashes on every 20th one.
    {
        WireFormat::Scope scope(WireFormat::Mode::Canonical);
        std::string       previous;
        for (std::size_t id = 0; id < Pack::SECTIONS_PER_PACK; ++id) {
            Section section { .id = SectionId(static_cast<long long>(id)) };
            if (id % 20 == 0) {
                std::string hashes;
                for (std::size_t member = id == 0 ? 0 : id - 19; member <= id; ++member)
                    hashes += Utils::calculate_hash(SectionId(static_cast<long long>(member)).to_string());
                auto control = Utils::calculate_hash(hashes);
                if (id != 0)
                    control = Utils::calculate_hash(previous + control);
                previous        = control;
                section.control = control;
            }
            TEST_REQUIRE(DagAdmissionTestFixture::store_raw_section(*dag, section));
        }
    }
    DagAdmissionTestFixture::start_chain_at_genesis(*dag);

    const auto pack_file = std::filesystem::path(ChainConst::DAG_PACKS_FOLDER) / "0.pack";
    TEST_REQUIRE(!std::filesystem::exists(pack_file));
    std::thread packer([&] {
        DagAdmissionTestFixture::pack_hot(*dag);
    });
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!DagAdmissionTestFixture::packing(*dag) && !std::filesystem::exists(pack_file)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    TEST_REQUIRE(!std::filesystem::exists(pack_file));
    const auto sync_started = std::chrono::steady_clock::now();
    DagAdmissionTestFixture::enter_sync(*dag);
    const auto waited = std::chrono::steady_clock::now() - sync_started;
    packer.join();
    std::printf("sync waited %lld ms for the pack lock\n",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(waited).count()));
    std::fflush(stdout);
    TEST_REQUIRE(!std::filesystem::exists(pack_file));

    // The range is sealed once the sync is over.
    DagAdmissionTestFixture::leave_sync(*dag);
    DagAdmissionTestFixture::pack_hot(*dag);
    TEST_REQUIRE(std::filesystem::exists(pack_file));

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
}
