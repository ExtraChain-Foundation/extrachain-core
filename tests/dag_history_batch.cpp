#include "chain/hot_section_store.h"
#include "core/extrachain_node.h"
#include "dag_admission_fixture.h"
#include "network/network_runtime.h"
#include "network/wire_format.h"
#include "test_support.h"
#include "utils/file_io.h"

int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-history-batch-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->network_runtime().stop();
    node->process();
    node->dag()->stop();
    auto&      dag     = *node->dag();
    const auto payload = [](int id, int amount) {
        Transaction transaction;
        transaction.set_section(SectionId(id));
        transaction.set_amount(BigNumberFloat(std::to_string(amount)));
        return Json::serialize(Section { .id = SectionId(id), .transactions = { transaction } });
    };
    for (int pack = 0; pack < 2; ++pack) {
        std::map<SectionId, std::string> sections;
        for (int id = pack * 32; id < (pack + 1) * 32; ++id)
            sections.emplace(SectionId(id), payload(id, id + 1));
        if (pack == 1)
            sections[SectionId(60)] = "invalid section";
        TEST_REQUIRE(
            Pack::write(std::filesystem::path(ChainConst::DAG_PACKS_FOLDER) / (std::to_string(pack) + ".pack"),
                        pack,
                        sections)
                .has_value());
    }
    DagAdmissionTestFixture::refresh_packs(dag);
    const auto loose = std::filesystem::path(dag.file_path(SectionId(31)));
    std::filesystem::create_directories(loose.parent_path());
    TEST_REQUIRE(FileIo::write_atomic(loose, payload(31, 100)).has_value());
    {
        HotSectionStore hot(std::filesystem::path(ChainConst::DAG_HOT_FOLDER) / "HotSections.db");
        TEST_REQUIRE(hot.put(SectionId(31), payload(31, 200)));
        TEST_REQUIRE(hot.put(SectionId(32), "invalid hot section"));
        TEST_REQUIRE(hot.put(SectionId(64), payload(64, 300)));
        const auto legacy = std::filesystem::path(dag.file_path(SectionId(32)));
        std::filesystem::create_directories(legacy.parent_path());
        TEST_REQUIRE(FileIo::write_atomic(legacy, payload(32, 400)).has_value());
    }
    for (const int first : { 0, 17, 31, 33, 60 }) {
        WireFormat::Scope scope(WireFormat::Mode::Legacy);
        const auto        batch = dag.read_section_batch(SectionId(first), SectionId(first + 31));
        for (int id = first; id <= first + 31; ++id) {
            const auto single = dag.read_section(SectionId(id));
            const auto found  = batch.find(SectionId(id));
            TEST_REQUIRE_EQ(found != batch.end(), single.has_value());
            if (single.has_value()) {
                WireFormat::Scope disk_scope(WireFormat::Mode::Canonical);
                TEST_REQUIRE_EQ(Json::serialize(found->second), Json::serialize(single.value()));
            }
        }
    }
    TEST_REQUIRE_EQ(dag.read_section_batch(SectionId(31), SectionId(31))
                        .at(SectionId(31))
                        .transactions.begin()
                        ->amount(),
                    BigNumberFloat("200"));
    TEST_REQUIRE_EQ(dag.read_section_batch(SectionId(32), SectionId(32))
                        .at(SectionId(32))
                        .transactions.begin()
                        ->amount(),
                    BigNumberFloat("400"));
    TEST_REQUIRE(dag.read_section_batch(SectionId(0), SectionId(32)).empty());
    TEST_REQUIRE(dag.read_section_batch(SectionId(-1), SectionId(0)).empty());
    TEST_REQUIRE(dag.read_section_batch(SectionId(1), SectionId(0)).empty());
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
}
