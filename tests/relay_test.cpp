#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "consensus/relay_transport.h"
#include "test_support.h"

using namespace ExtraChain::Consensus;

int main() {
    Actor<KeyPrivate> governance;
    governance.create(ActorType::Service);
    std::vector<ValidatorRecord>   records;
    std::vector<ValidatorIdentity> identities;
    for (int index = 0; index < 7; ++index) {
        Actor<KeyPrivate> actor;
        actor.create(ActorType::Service);
        KeyPrivate key;
        key.generate_random();
        const auto record =
            make_validator_record(governance.id(), 1, actor, key, "node-" + std::to_string(index), 0);
        TEST_REQUIRE(record.has_value());
        identities.push_back({ record.value().validator_id, key });
        records.push_back(record.value());
    }
    const auto document = make_validator_set(governance.id(), 1, records, governance);
    TEST_REQUIRE(document.has_value());
    const auto view = ValidatorSetView::create(document.value());
    TEST_REQUIRE(view.has_value());
    constexpr std::uint64_t now     = 1000;
    const auto              message = RelayTransport::create(view.value(),
                                                identities[0],
                                                MessageType::ConsensusVote,
                                                MessageStatus::NoStatus,
                                                "signed vote bytes",
                                                             {},
                                                             {},
                                                now);
    TEST_REQUIRE(message.has_value());
    {
        RelayTransport duplicates;
        const auto     wire = MessagePack::serialize(message.value());
        TEST_REQUIRE(!duplicates.should_drop_duplicate(wire, now));
        auto corrupt = message.value();
        corrupt.payload += "changed";
        TEST_REQUIRE(!duplicates.accept(corrupt, view.value(), "node-1", "node-0", {}, now).deliver);
        TEST_REQUIRE(!duplicates.should_drop_duplicate(wire, now));
        TEST_REQUIRE(duplicates.accept(message.value(), view.value(), "node-1", "node-0", {}, now).deliver);
        TEST_REQUIRE(duplicates.should_drop_duplicate(wire, now));
        TEST_REQUIRE(duplicates.should_drop_duplicate(MessagePack::serialize(corrupt), now));
        corrupt = message.value();
        corrupt.signature.back() ^= 1;
        TEST_REQUIRE(duplicates.should_drop_duplicate(MessagePack::serialize(corrupt), now));
        TEST_REQUIRE(!RelayTransport::valid(corrupt, view.value(), now));
        for (int mutation = 0; mutation < 5; ++mutation) {
            auto different = message.value();
            if (mutation == 0)
                ++different.epoch;
            if (mutation == 1)
                different.network_id = records[0].actor_id;
            if (mutation == 2)
                different.origin = identities[1].validator_id;
            if (mutation == 3)
                different.request_id = "new-request";
            if (mutation == 4)
                ++different.expires_ms;
            TEST_REQUIRE(!duplicates.should_drop_duplicate(MessagePack::serialize(different), now));
        }
        for (const auto& malformed : { std::string(),
                                       std::string(4097, 'x'),
                                       std::string("\xdd\xff\xff\xff\xff", 5),
                                       std::string("\xdf\xff\xff\xff\xff", 5),
                                       std::string("\xdb\xff\xff\xff\xff", 5),
                                       std::string(32, '\x91') }) {
            TEST_REQUIRE(!duplicates.should_drop_duplicate(malformed, now));
        }
        TEST_REQUIRE(duplicates.should_drop_duplicate(wire, message.value().expires_ms - 1));
        TEST_REQUIRE(!duplicates.should_drop_duplicate(wire, message.value().expires_ms));
        duplicates.clear();
        TEST_REQUIRE(!duplicates.should_drop_duplicate(wire, now));
        TEST_REQUIRE(duplicates.accept(message.value(), view.value(), "node-1", "node-0", {}, now).deliver);
        const auto large = RelayTransport::create(view.value(),
                                                  identities[0],
                                                  MessageType::ConsensusVote,
                                                  MessageStatus::NoStatus,
                                                  std::string(8192, 'x'),
                                                  {},
                                                  {},
                                                  now);
        TEST_REQUIRE(large.has_value());
        TEST_REQUIRE(duplicates.accept(large.value(), view.value(), "node-1", "node-0", {}, now).deliver);
        TEST_REQUIRE(!duplicates.should_drop_duplicate(MessagePack::serialize(large.value()), now));
        const auto dropped = duplicates.accept(large.value(), view.value(), "node-1", "node-0", { "node-2" }, now);
        TEST_REQUIRE(!dropped.deliver && dropped.peers.empty());
    }
    for (bool ring : { false, true }) {
        std::map<std::string, RelayTransport>           routers;
        std::map<std::string, std::vector<std::string>> edges;
        for (int index = 0; index < 8; ++index) {
            const auto name = "node-" + std::to_string(index);
            if (index > 0)
                edges[name].push_back("node-" + std::to_string(index - 1));
            if (index < 7)
                edges[name].push_back("node-" + std::to_string(index + 1));
        }
        if (ring) {
            edges["node-0"].push_back("node-7");
            edges["node-7"].push_back("node-0");
        }
        struct Packet {
            std::string   to;
            std::string   from;
            RelayEnvelope value;
        };
        std::deque<Packet>    queue { { "node-0", {}, message.value() } };
        std::set<std::string> delivered;
        std::size_t           traversals = 0;
        while (!queue.empty()) {
            auto packet = std::move(queue.front());
            queue.pop_front();
            const auto result = routers[packet.to].accept(packet.value,
                                                          view.value(),
                                                          packet.to,
                                                          packet.from,
                                                          edges[packet.to],
                                                          now);
            if (result.deliver)
                TEST_REQUIRE(delivered.insert(packet.to).second);
            for (const auto& peer : result.peers) {
                auto forwarded = packet.value;
                ++forwarded.hops;
                queue.push_back({ peer, packet.to, forwarded });
                TEST_REQUIRE(++traversals < 32);
            }
        }
        TEST_REQUIRE_EQ(delivered.size(), std::size_t(8));
    }
    RelayTransport receiver;
    auto           corrupt = message.value();
    corrupt.payload += "changed";
    TEST_REQUIRE(!receiver.accept(corrupt, view.value(), "node-1", "node-0", {}, now).deliver);
    TEST_REQUIRE(receiver.accept(message.value(), view.value(), "node-1", "node-0", {}, now).deliver);
    TEST_REQUIRE(!receiver.accept(message.value(), view.value(), "node-1", "node-0", {}, now).deliver);
    auto changed_copy = message.value();
    changed_copy.signature.back() ^= 1;
    const auto dropped = receiver.accept(changed_copy, view.value(), "node-1", "node-0", { "node-2" }, now);
    TEST_REQUIRE(!dropped.deliver && dropped.peers.empty());
    receiver.clear();
    TEST_REQUIRE(!receiver.accept(changed_copy, view.value(), "node-1", "node-0", {}, now).deliver);
    TEST_REQUIRE(receiver.accept(message.value(), view.value(), "node-1", "node-0", {}, now).deliver);
    TEST_REQUIRE(
        !receiver.accept(message.value(), view.value(), "node-1", "node-0", {}, message.value().expires_ms)
             .deliver);
    const auto renewed = RelayTransport::create(view.value(),
                                                identities[0],
                                                MessageType::ConsensusVote,
                                                MessageStatus::NoStatus,
                                                "signed vote bytes",
                                                {},
                                                {},
                                                message.value().expires_ms);
    TEST_REQUIRE(renewed.has_value());
    TEST_REQUIRE(receiver.accept(renewed.value(), view.value(), "node-1", "node-0", {}, message.value().expires_ms)
                     .deliver);
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto invalid = message.value();
        if (mutation == 0)
            invalid.origin = identities[1].validator_id;
        if (mutation == 1)
            invalid.epoch++;
        if (mutation == 2)
            invalid.destination = "node-1";
        if (mutation == 3)
            invalid.hops = RelayTransport::MaximumHops;
        if (mutation == 4)
            invalid.signature.back() ^= 1;
        if (mutation == 5)
            invalid.expires_ms = now;
        TEST_REQUIRE(!RelayTransport::valid(invalid, view.value(), now));
    }
    const auto addressed = RelayTransport::create(view.value(),
                                                  identities[0],
                                                  MessageType::ConsensusVote,
                                                  MessageStatus::NoStatus,
                                                  "addressed vote",
                                                  "node-6",
                                                  {},
                                                  now);
    TEST_REQUIRE(addressed.has_value());
    for (const bool authenticated : { false, true }) {
        RelayTransport router;
        const auto     routed =
            router
                .accept(addressed.value(), view.value(), "node-0", {}, { "node-1", "node-6" }, now, authenticated);
        TEST_REQUIRE(!routed.deliver);
        TEST_REQUIRE_EQ(routed.peers.size(), authenticated ? std::size_t(1) : std::size_t(2));
        if (authenticated)
            TEST_REQUIRE_EQ(routed.peers.front(), std::string("node-6"));
    }
    RelayTransport disconnected;
    const auto     fallback =
        disconnected.accept(addressed.value(), view.value(), "node-0", {}, { "node-1", "node-2" }, now, true);
    TEST_REQUIRE_EQ(fallback.peers, (std::vector<std::string> { "node-1", "node-2" }));
    RelayTransport broadcast_router;
    const auto     broadcast = broadcast_router.accept(message.value(),
                                                   view.value(),
                                                   "node-1",
                                                   "node-0",
                                                       { "node-0", "node-2", "node-6" },
                                                   now,
                                                   true);
    TEST_REQUIRE(broadcast.deliver);
    TEST_REQUIRE_EQ(broadcast.peers, (std::vector<std::string> { "node-2", "node-6" }));

    RelayTransport requester, middle, server;
    const auto     request = RelayTransport::create(view.value(),
                                                identities[0],
                                                MessageType::ConsensusBatchRequest,
                                                MessageStatus::Request,
                                                "batch hash",
                                                "node-6",
                                                    {},
                                                now);
    TEST_REQUIRE(request.has_value());
    TEST_REQUIRE_EQ(requester.accept(request.value(), view.value(), "node-0", {}, { "relay" }, now).peers.size(),
                    std::size_t(1));
    TEST_REQUIRE_EQ(middle.accept(request.value(), view.value(), "relay", "node-0", { "node-0", "node-6" }, now)
                        .peers.size(),
                    std::size_t(1));
    TEST_REQUIRE(server.accept(request.value(), view.value(), "node-6", "relay", { "relay" }, now).deliver);
    const auto response = RelayTransport::create(view.value(),
                                                 identities[6],
                                                 MessageType::ConsensusBatchData,
                                                 MessageStatus::Response,
                                                 "batch contents",
                                                 "node-0",
                                                 request.value().request_id,
                                                 now + 1);
    TEST_REQUIRE(response.has_value());
    const auto back =
        middle.accept(response.value(), view.value(), "relay", "node-6", { "node-0", "node-6" }, now + 1);
    TEST_REQUIRE_EQ(back.peers.size(), std::size_t(1));
    TEST_REQUIRE_EQ(back.peers.front(), std::string("node-0"));
    TEST_REQUIRE(requester.accept(response.value(), view.value(), "node-0", "relay", {}, now + 1).deliver);
    RelayTransport unsolicited;
    TEST_REQUIRE(!unsolicited.accept(response.value(), view.value(), "node-0", "relay", {}, now + 1).deliver);
    RelayTransport direct_requester, direct_server;
    const auto     direct_request =
        direct_requester.accept(request.value(), view.value(), "node-0", {}, { "node-1", "node-6" }, now, true);
    TEST_REQUIRE_EQ(direct_request.peers, (std::vector<std::string> { "node-6" }));
    TEST_REQUIRE(direct_server.accept(request.value(), view.value(), "node-6", "node-0", {}, now).deliver);
    const auto direct_response =
        direct_server.accept(response.value(), view.value(), "node-6", {}, { "node-0", "node-1" }, now + 1, true);
    TEST_REQUIRE_EQ(direct_response.peers, (std::vector<std::string> { "node-0" }));
    TEST_REQUIRE(direct_requester.accept(response.value(), view.value(), "node-0", "node-6", {}, now + 1).deliver);

    const auto too_large = RelayTransport::create(view.value(),
                                                  identities[0],
                                                  MessageType::ConsensusVote,
                                                  MessageStatus::NoStatus,
                                                  std::string(1024 * 1024 + 1, 'x'),
                                                  {},
                                                  {},
                                                  now);
    TEST_REQUIRE(!too_large.has_value());
    std::puts("relay verification: PASS");
}
