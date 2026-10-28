#include "consensus/consensus_protocol.h"
#include "test_support.h"
#include "utils/exc_utils.h"

#include <algorithm>
#include <bit>
#include <map>
#include <vector>

using namespace ExtraChain::Consensus;

int main() {
    for (std::size_t count = 0; count <= 7; ++count) {
        std::vector<std::string> leaves;
        for (std::size_t index = 0; index < count; ++index)
            leaves.push_back("legacy_" + std::to_string(index));
        const auto pairs = Utils::splitListIntoPair(leaves, false);
        TEST_REQUIRE_EQ(pairs.size(), count / 2 + count % 2);
        for (std::size_t index = 0; index < pairs.size(); ++index) {
            TEST_REQUIRE_EQ(pairs[index].size(), std::min<std::size_t>(2, count - 2 * index));
            for (std::size_t offset = 0; offset < pairs[index].size(); ++offset)
                TEST_REQUIRE_EQ(pairs[index][offset], leaves[2 * index + offset]);
        }
    }
    std::string single = "one leaf";
    TEST_REQUIRE_EQ(Utils::rootMerkleHash(single), Utils::calculate_hash(single));
    for (const std::size_t count : { 1, 2, 3, 5, 8, 15, 16, 17, 63, 64, 65, 1025 }) {
        std::vector<std::string> values;
        for (std::size_t index = 0; index < count; ++index)
            values.push_back("value_" + std::to_string(index));
        std::vector<std::uint64_t> targets;
        for (std::size_t index = 0; index < count; index += std::max<std::size_t>(1, count / 8))
            targets.push_back(index);
        if (targets.back() != count - 1)
            targets.push_back(count - 1);
        std::vector<std::string> emitted;
        const auto               read = [&](std::uint64_t index) -> std::expected<std::string, ConsensusError> {
            return values.at(index);
        };
        const auto stream = build_merkle_tree(count, read, targets, [&](auto begin, auto height, auto hash) {
            const auto end    = begin + (std::uint64_t(1) << height);
            const auto offset = 2 * end - std::popcount(end) - (std::countr_zero(end) - height) - 1;
            TEST_REQUIRE_EQ(offset, std::uint64_t(emitted.size()));
            emitted.emplace_back(hash);
            return true;
        });
        TEST_REQUIRE(stream.has_value());
        TEST_REQUIRE_EQ(stream.value().root, merkle_root(values));
        TEST_REQUIRE_EQ(emitted.size(), 2 * count - std::popcount(count));
        TEST_REQUIRE_EQ(stream.value().proofs.size(), targets.size());
        std::size_t node_reads = 0;
        const auto  read_node  = [&](std::uint64_t begin,
                                     std::uint32_t height) -> std::expected<std::string, ConsensusError> {
            ++node_reads;
            const auto end    = begin + (std::uint64_t(1) << height);
            const auto offset = 2 * end - std::popcount(end) - (std::countr_zero(end) - height) - 1;
            return emitted.at(offset);
        };
        for (std::size_t index = 0; index < targets.size(); ++index) {
            auto        expected = make_merkle_proof(values, targets[index]).value();
            const auto& actual   = stream.value().proofs[index];
            TEST_REQUIRE_EQ(actual.leaf_index, expected.leaf_index);
            TEST_REQUIRE_EQ(actual.leaf_count, expected.leaf_count);
            TEST_REQUIRE_EQ(actual.leaf_hash, expected.leaf_hash);
            TEST_REQUIRE_EQ(actual.siblings, expected.siblings);
            TEST_REQUIRE(verify_merkle_proof(values[targets[index]], actual, stream.value().root));
            node_reads         = 0;
            const auto indexed = make_indexed_merkle_proof(count, targets[index], read_node);
            TEST_REQUIRE(indexed.has_value());
            TEST_REQUIRE_EQ(indexed.value().leaf_hash, expected.leaf_hash);
            TEST_REQUIRE_EQ(indexed.value().siblings, expected.siblings);
            TEST_REQUIRE(node_reads <= 2 * std::bit_width(count));
        }
        TEST_REQUIRE_EQ(build_merkle_tree(count, read).value().root, stream.value().root);
        TEST_REQUIRE(!build_merkle_tree(count, read, { count }).has_value());
        TEST_REQUIRE(!build_merkle_tree(count, read, { 0, 0 }).has_value());
        TEST_REQUIRE(!build_merkle_tree(count, read, { }, [](auto, auto, auto) {
                          return false;
                      }).has_value());
    }
    const auto reader = [](std::uint64_t) -> std::expected<std::string, ConsensusError> {
        return "value";
    };
    TEST_REQUIRE(!build_merkle_tree(0, reader).has_value());
    TEST_REQUIRE(!build_merkle_tree(UINT64_MAX, reader).has_value());
    const auto node_reader = [](auto, auto) -> std::expected<std::string, ConsensusError> {
        return std::string(64, 'a');
    };
    TEST_REQUIRE(!make_indexed_merkle_proof(0, 0, node_reader).has_value());
    TEST_REQUIRE(!make_indexed_merkle_proof(UINT64_MAX, 0, node_reader).has_value());
    TEST_REQUIRE(!make_indexed_merkle_proof(1, 1, node_reader).has_value());
    TEST_REQUIRE(!make_indexed_merkle_proof(1, 0, { }).has_value());
    TEST_REQUIRE(!make_indexed_merkle_proof(1, 0, [](auto, auto) -> std::expected<std::string, ConsensusError> {
                      return "broken";
                  }).has_value());
    std::size_t large_reads = 0;
    TEST_REQUIRE(
        make_indexed_merkle_proof((std::uint64_t(1) << 32) - 1,
                                  0,
                                  [&](auto begin, auto height) -> std::expected<std::string, ConsensusError> {
                                      ++large_reads;
                                      return node_reader(begin, height);
                                  })
            .has_value());
    TEST_REQUIRE(large_reads <= 64);
    TEST_REQUIRE(!build_merkle_tree(1, [](auto) -> std::expected<std::string, ConsensusError> {
                      return std::unexpected(ConsensusError::DataUnavailable);
                  }).has_value());
    std::vector<std::string> values { "one", "two", "three", "four" };
    auto                     false_count = make_merkle_proof(values, 2).value();
    false_count.leaf_count               = 3;
    TEST_REQUIRE(!verify_merkle_proof("three", false_count, merkle_root(values)));
    false_count.leaf_count = UINT64_MAX;
    TEST_REQUIRE(!verify_merkle_proof("three", false_count, merkle_root(values)));
}
