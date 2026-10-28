#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "dag_admission_fixture.h"
#include "network/network_runtime.h"
#include "test_support.h"

// Installing a received pack replays the whole history into the balance cache. A node that
// rejoins after its peers sealed a range already holds those sections; the replay is needed
// only when the pack disagrees with them.
int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-pack-install-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->network_runtime().stop();
    node->process();
    auto* dag = node->dag();
    dag->set_mode(DagMode::Full);

    // Empty sections with valid control hashes on every 20th one, as a peer would pack them.
    std::map<SectionId, Section> sections;
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
            sections.emplace(section.id, section);
        }
    }
    const auto received_path = home / "received.pack";
    {
        WireFormat::Scope scope(WireFormat::Mode::Canonical);
        TEST_REQUIRE(Pack::write(received_path,
                                 0,
                                 SectionId(0),
                                 SectionId(static_cast<long long>(Pack::SECTIONS_PER_PACK - 1)),
                                 [&](const SectionId& id) -> std::optional<std::string> {
                                     return Json::serialize(sections.at(id));
                                 })
                         .has_value());
    }
    const auto received = Pack::Reader::open(received_path);
    TEST_REQUIRE(received.has_value());

    // The node holds the same sections; sync left the empty ones out.
    {
        WireFormat::Scope scope(WireFormat::Mode::Canonical);
        for (const auto& [id, section] : sections) {
            if (section.control.has_value())
                TEST_REQUIRE(DagAdmissionTestFixture::store_raw_section(*dag, section));
        }
    }
    TEST_REQUIRE(DagAdmissionTestFixture::accept_received_pack(*dag, 0, received.value()));
    TEST_REQUIRE(!DagAdmissionTestFixture::pack_history_dirty(*dag));

    // A local section that disagrees with the pack makes the replay necessary.
    {
        WireFormat::Scope scope(WireFormat::Mode::Canonical);
        auto              stale = sections.at(SectionId(4320));
        stale.control           = std::string(64, 'a');
        TEST_REQUIRE(DagAdmissionTestFixture::store_raw_section(*dag, stale));
    }
    TEST_REQUIRE(DagAdmissionTestFixture::accept_received_pack(*dag, 0, received.value()));
    TEST_REQUIRE(DagAdmissionTestFixture::pack_history_dirty(*dag));

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
}
