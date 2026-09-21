#include "consensus/mining_state.h"
#include "mining_format_fixtures.h"
#include "test_support.h"
#include "utils/serialization.h"

using namespace ExtraChain::Consensus;

namespace {
    template <std::size_t Size>
    std::string bytes(const char (&value)[Size]) {
        return std::string(value, Size - 1);
    }

    void check_fixture(const std::string& encoded_epoch,
                       const std::string& encoded_snapshot,
                       const std::string& encoded_witness,
                       std::string_view   epoch_root,
                       std::string_view   state_root,
                       bool               claimed) {
        auto epoch = MessagePack::deserialize<MiningEpochState>(encoded_epoch);
        TEST_REQUIRE(epoch.has_value());
        TEST_REQUIRE_EQ(MessagePack::serialize(epoch.value()), encoded_epoch);
        TEST_REQUIRE_EQ(mining_epoch_root(epoch.value()), epoch_root);
        TEST_REQUIRE_EQ(epoch.value().claimed.size(), std::size_t(claimed));
        TEST_REQUIRE_EQ(epoch.value().rewards.at(std::string(40, 'b')), std::uint64_t(7));
        TEST_REQUIRE(!settle_mining_epoch(epoch.value(), 180).has_value());
        TEST_REQUIRE_EQ(MessagePack::serialize(epoch.value()), encoded_epoch);

        using Snapshot      = std::pair<std::string, MiningState>;
        const auto snapshot = MessagePack::deserialize<Snapshot>(encoded_snapshot);
        TEST_REQUIRE(snapshot.has_value());
        TEST_REQUIRE_EQ(MessagePack::serialize(snapshot.value()), encoded_snapshot);
        TEST_REQUIRE_EQ(mining_state_root(snapshot.value().second), state_root);
        const auto witness = MessagePack::deserialize<MiningEpochWitness>(encoded_witness);
        TEST_REQUIRE(witness.has_value());
        TEST_REQUIRE_EQ(MessagePack::serialize(witness.value()), encoded_witness);
        TEST_REQUIRE(verify_mining_epoch_witness(witness.value(), state_root));
        TEST_REQUIRE_EQ(MessagePack::serialize(make_mining_epoch_witness(snapshot.value().second, 0).value()),
                        encoded_witness);
        auto changed = witness.value();
        changed.epoch.claimed =
            claimed ? std::set<std::string> { } : std::set<std::string> { std::string(40, 'b') };
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, state_root));
        changed.epoch.claimed = { "invalid" };
        TEST_REQUIRE(!verify_mining_epoch_witness(changed, state_root));
        auto invalid                 = snapshot.value().second;
        invalid.epochs.at(0).claimed = { "invalid" };
        TEST_REQUIRE(mining_state_root(invalid).empty());

        // A partially claimed record must never become a fresh automatic payout.
        auto pending      = epoch.value();
        pending.settled   = false;
        const auto before = MessagePack::serialize(pending);
        TEST_REQUIRE(!settle_mining_epoch(pending, 180).has_value());
        TEST_REQUIRE_EQ(MessagePack::serialize(pending), before);
        pending.rewards.clear();
        pending.claimed           = { std::string(40, 'b') };
        pending.challenge         = StorageChallenge { 0, std::string(64, 'd') };
        const auto claimed_before = MessagePack::serialize(pending);
        TEST_REQUIRE(!settle_mining_epoch(pending, 180).has_value());
        TEST_REQUIRE_EQ(MessagePack::serialize(pending), claimed_before);

        invalid.epochs.at(0).claimed.clear();
        for (std::size_t index = 0; index <= MaximumMiningRegistrations; ++index)
            invalid.epochs.at(0).claimed.insert(fmt::format("{:040x}", index));
        TEST_REQUIRE(mining_state_root(invalid).empty());
    }
} // namespace

int main() {
    using namespace MiningFormatFixtures;
    check_fixture(bytes(EmptyEpoch),
                  bytes(EmptySnapshot),
                  bytes(EmptyWitness),
                  EmptyEpochRoot,
                  EmptyStateRoot,
                  false);
    check_fixture(bytes(ClaimedEpoch),
                  bytes(ClaimedSnapshot),
                  bytes(ClaimedWitness),
                  ClaimedEpochRoot,
                  ClaimedStateRoot,
                  true);

    auto short_epoch = bytes(EmptyEpoch);
    short_epoch[0]   = char(0x99);
    short_epoch.pop_back();
    TEST_REQUIRE(!MessagePack::deserialize<MiningEpochState>(short_epoch).has_value());
    auto long_epoch = bytes(EmptyEpoch);
    long_epoch[0]   = char(0x9b);
    long_epoch.push_back(char(0xc0));
    TEST_REQUIRE(!MessagePack::deserialize<MiningEpochState>(long_epoch).has_value());
    auto wrong_type   = bytes(EmptyEpoch);
    wrong_type.back() = char(0x80);
    TEST_REQUIRE(!MessagePack::deserialize<MiningEpochState>(wrong_type).has_value());
    TEST_REQUIRE(!MessagePack::deserialize<MiningEpochState>(std::string(1, char(0x80))).has_value());

    // Strict epoch decoding applies inside snapshots and received witnesses as well.
    for (const auto& bad : { short_epoch, long_epoch, wrong_type }) {
        auto       snapshot = bytes(EmptySnapshot);
        const auto offset   = snapshot.find(bytes(EmptyEpoch));
        TEST_REQUIRE(offset != std::string::npos);
        snapshot.replace(offset, sizeof(EmptyEpoch) - 1, bad);
        TEST_REQUIRE((!MessagePack::deserialize<std::pair<std::string, MiningState>>(snapshot).has_value()));
        auto witness = bytes(EmptyWitness);
        witness.replace(1, sizeof(EmptyEpoch) - 1, bad);
        TEST_REQUIRE(!MessagePack::deserialize<MiningEpochWitness>(witness).has_value());
    }
}
