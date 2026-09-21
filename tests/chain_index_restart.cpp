#include "chain/chain_index.h"
#include "chain/hot_section_store.h"
#include "core/extrachain_node.h"
#include "test_support.h"

#include <cstdlib>
#include <thread>

int main(int argc, char* argv[]) {
    TEST_REQUIRE_EQ(argc, 3);
    std::filesystem::current_path(argv[2]);
    const std::string_view phase(argv[1]);
    if (phase == "clean" || phase == "incomplete" || phase == "invalidate") {
        ChainIndex index(nullptr);
        if (phase == "invalidate") {
            TEST_REQUIRE(index.derived_index_ready());
            TEST_REQUIRE(index.invalidate_derived_index());
        } else {
            TEST_REQUIRE_EQ(index.derived_index_ready(), phase == "clean");
        }
        TEST_REQUIRE_EQ(index.row_count(), std::uint64_t(2));
        return 0;
    }
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    auto* index = node->dag()->chain_index();
    TEST_REQUIRE(index != nullptr);
    if (phase == "crash") {
        node->dag()->stop();
        Actor<KeyPrivate> owner;
        owner.create(ActorType::User);
        Transaction transaction;
        transaction.set_sender(owner.id());
        transaction.set_receiver(owner.id());
        transaction.set_token(owner.id());
        transaction.set_type(TransactionType::Balance);
        transaction.set_amount(BigNumberFloat(10));
        transaction.set_section(SectionId(0));
        TEST_REQUIRE(transaction.sign(owner));
        const Section first { .id = SectionId(0), .transactions = { transaction } };
        index->on_section_written(first);
        index->flush();
        TEST_REQUIRE_EQ(index->row_count(), std::uint64_t(1));
        transaction.set_section(SectionId(21));
        TEST_REQUIRE(transaction.sign(owner));
        const Section   last { .id = SectionId(21), .transactions = { transaction } };
        HotSectionStore hot(std::filesystem::path(ChainConst::DAG_HOT_FOLDER) / "HotSections.db");
        TEST_REQUIRE(hot.commit_batch({ { first.id, Json::serialize(first) }, { last.id, Json::serialize(last) } },
                                      std::pair { first.id, last.id }));
        std::_Exit(0);
    }
    TEST_REQUIRE_EQ(phase, std::string_view("recover"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!index->derived_index_ready() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    TEST_REQUIRE(index->derived_index_ready());
    TEST_REQUIRE_EQ(index->row_count(), std::uint64_t(2));
    TEST_REQUIRE_EQ(node->dag()->current_section(), SectionId(21));
    node->cleanUp();
}
