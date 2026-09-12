#include "runtime/work_budget.h"
#include "test_support.h"

#include <barrier>
#include <thread>
#include <vector>

int main() {
    using ExtraChain::Core::WorkBudget;
    WorkBudget bytes({ 100, 4, 75, 2 });
    auto       first = bytes.reserve("first", 75);
    TEST_REQUIRE(first);
    TEST_REQUIRE(!bytes.reserve("first", 1));
    TEST_REQUIRE(!bytes.reserve("second", 26));
    auto second = bytes.reserve("second", 25);
    TEST_REQUIRE(second);
    TEST_REQUIRE(!bytes.reserve("third", 1));
    auto retained = first;
    first.reset();
    TEST_REQUIRE(!bytes.reserve("third", 1));
    retained.reset();
    TEST_REQUIRE(bytes.reserve("third", 75));
    TEST_REQUIRE(!bytes.reserve(std::string(65, 'x'), 1));

    WorkBudget                                       counts({ 100, 4, 100, 2 });
    std::vector<std::shared_ptr<WorkBudget::Ticket>> tickets(16);
    std::barrier                                     gate(17);
    std::vector<std::thread>                         workers;
    for (unsigned i = 0; i < 16; ++i)
        workers.emplace_back([&, i] {
            tickets[i] = counts.reserve(std::to_string(i % 2), 1);
            gate.arrive_and_wait();
        });
    gate.arrive_and_wait();
    std::size_t accepted = 0;
    for (const auto& ticket : tickets)
        accepted += ticket ? 1 : 0;
    TEST_REQUIRE_EQ(accepted, std::size_t(4));
    for (auto& worker : workers)
        worker.join();
    tickets.clear();
    TEST_REQUIRE(counts.reserve("0", 1));

    auto shutdown = std::make_unique<WorkBudget>(WorkBudget::Limits { 100, 4, 75, 2 });
    auto held     = shutdown->reserve("peer", 1);
    TEST_REQUIRE(held && !held->stopped());
    shutdown->stop();
    TEST_REQUIRE(held->stopped() && !shutdown->reserve("peer", 1));
    shutdown.reset();
    TEST_REQUIRE(held->stopped());
    held.reset();
    return 0;
}
