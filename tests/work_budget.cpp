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

    WorkBudget pending({ 10, 1, 10, 1 });
    auto       running = pending.reserve("peer", 6);
    TEST_REQUIRE(running);
    auto waiting = pending.reserve_waiting("peer", 4);
    TEST_REQUIRE(waiting && !waiting->try_start());
    TEST_REQUIRE(!pending.reserve_waiting("peer", 0));
    TEST_REQUIRE(!pending.reserve_waiting("other", 1));
    TEST_REQUIRE(!pending.reserve("other", 0));
    running.reset();
    TEST_REQUIRE(!pending.reserve("other", 7));
    TEST_REQUIRE(!pending.reserve("peer", 0));
    TEST_REQUIRE(waiting->try_start());
    TEST_REQUIRE(waiting->try_start());
    auto empty_waiter = pending.reserve_waiting("other", 0);
    TEST_REQUIRE(empty_waiter && !empty_waiter->try_start());
    TEST_REQUIRE(!pending.reserve_waiting("third", 0));
    waiting.reset();
    TEST_REQUIRE(empty_waiter->try_start());
    auto stopped_waiter = pending.reserve_waiting("last", 1);
    TEST_REQUIRE(stopped_waiter && !stopped_waiter->try_start());
    pending.stop();
    TEST_REQUIRE(!empty_waiter->try_start());
    TEST_REQUIRE(!stopped_waiter->try_start());
    empty_waiter.reset();
    stopped_waiter.reset();

    WorkBudget cancelled({ 10, 1, 10, 1 });
    auto       busy             = cancelled.reserve("peer", 6);
    auto       cancelled_waiter = cancelled.reserve_waiting("peer", 4);
    TEST_REQUIRE(busy && cancelled_waiter && !cancelled_waiter->try_start());
    cancelled_waiter.reset();
    TEST_REQUIRE(cancelled.reserve_waiting("other", 4));

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
