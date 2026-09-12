#include "network/broadcast_budget.h"
#include "test_support.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

int main() {
    using Budget                 = Network::BroadcastBudget;
    const auto               now = Budget::Clock::time_point(100s);
    Budget                   per_peer;
    std::atomic_size_t       accepted { 0 };
    std::vector<std::thread> senders;
    for (unsigned i = 0; i < 8; ++i)
        senders.emplace_back([&] {
            for (unsigned j = 0; j < 1000; ++j)
                accepted += per_peer.accept("peer", 1, now) ? 1 : 0;
        });
    for (auto& sender : senders)
        sender.join();
    TEST_REQUIRE_EQ(accepted.load(), Budget::PeerMessages);
    TEST_REQUIRE(!per_peer.accept("peer", 1, now + 999ms));
    TEST_REQUIRE(per_peer.accept("other", 1, now));
    TEST_REQUIRE(per_peer.accept("peer", 1, now + 1s));

    Budget bytes;
    TEST_REQUIRE(bytes.accept("peer", Budget::PeerBytes, now));
    TEST_REQUIRE(!bytes.accept("peer", 1, now));
    TEST_REQUIRE(bytes.accept("other", 1, now));
    TEST_REQUIRE(!bytes.accept("too-large", Budget::PeerBytes + 1, now));
    TEST_REQUIRE(!bytes.accept("", 1, now));
    TEST_REQUIRE(!bytes.accept(std::string(65, 'a'), 1, now));

    Budget total;
    for (unsigned i = 0; i < 8; ++i)
        TEST_REQUIRE(total.accept(std::to_string(i), Budget::PeerBytes, now));
    TEST_REQUIRE(!total.accept("new", 1, now));
    TEST_REQUIRE(total.accept("new", 1, now + 1s));

    Budget peers;
    for (unsigned i = 0; i < Budget::MaxPeers; ++i)
        TEST_REQUIRE(peers.accept(std::to_string(i), 1, now));
    TEST_REQUIRE(!peers.accept("new", 1, now));
    TEST_REQUIRE(!peers.accept("new", 1, now + 1s));
    TEST_REQUIRE(peers.accept("0", 1, now + 1s));
    TEST_REQUIRE(peers.accept("new", 1, now + 31s));
    return 0;
}
