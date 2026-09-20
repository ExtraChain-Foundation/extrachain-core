#include "chain/hot_section_store.h"
#include "core/extrachain_node.h"
#include "dag_admission_fixture.h"
#include "network/network_runtime.h"
#include "test_support.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: extrachain-pack-history <isolated-home> <pack-id>\n");
        return 64;
    }
    std::filesystem::current_path(argv[1]);
    const auto id = SectionId::create(argv[2]);
    TEST_REQUIRE(id.has_value());
    const auto number = id.value().to_int();
    TEST_REQUIRE(number.has_value() && number.value() >= 0);
    const auto pack_id = static_cast<Pack::PackId>(number.value());
    const auto first   = id.value() * Pack::SECTIONS_PER_PACK;
    const auto last    = first + Pack::SECTIONS_PER_PACK - 1;
    const auto path    = std::filesystem::path(ChainConst::DAG_PACKS_FOLDER) / (std::to_string(pack_id) + ".pack");
    TEST_REQUIRE(!std::filesystem::exists(path));
    std::map<SectionId, std::string> hashes;
    std::uint64_t                    payload_bytes = 0;
    {
        HotSectionStore hot(std::filesystem::path(ChainConst::DAG_HOT_FOLDER) / "HotSections.db");
        TEST_REQUIRE(hot.is_open());
        for (auto section = first; section <= last; section += 1) {
            const auto payload = hot.get(section);
            TEST_REQUIRE(payload.has_value());
            payload_bytes += payload.value().size();
            hashes.emplace(section, Utils::calculate_hash(payload.value()));
        }
    }
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->network_runtime().stop();
    node->process();
    const auto login = node->login(Utils::calculate_hash(std::string("gen-login") + "gen-password"));
    TEST_REQUIRE(login.has_value());
    node->dag()->stop();
    const auto reader = Pack::Reader::open(path);
    TEST_REQUIRE(reader.has_value());
    for (auto frame = first; frame <= last; frame += Pack::SECTIONS_PER_FRAME) {
        const auto end  = std::min(last, frame + Pack::SECTIONS_PER_FRAME - 1);
        const auto rows = reader.value().read_range(frame, end);
        TEST_REQUIRE_EQ(SectionId(rows.size()), end - frame + 1);
        for (const auto &[section, payload] : rows)
            TEST_REQUIRE_EQ(Utils::calculate_hash(payload), hashes.at(section));
    }
    TEST_REQUIRE(DagAdmissionTestFixture::pack(*node->dag(), pack_id, reader.value()));
    {
        HotSectionStore hot(std::filesystem::path(ChainConst::DAG_HOT_FOLDER) / "HotSections.db");
        TEST_REQUIRE(hot.read_range(first, last).empty());
    }
    node->cleanUp();
    std::printf("PASS pack=%llu sections=%zu payload_bytes=%llu\n",
                static_cast<unsigned long long>(pack_id),
                hashes.size(),
                static_cast<unsigned long long>(payload_bytes));
}
