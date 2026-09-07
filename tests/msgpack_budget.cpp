#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "contracts/contract_codec.h"
#include "test_support.h"
#include "utils/serialization.h"

namespace {
    struct Values {
        std::vector<int> items;
        MSGPACK_DEFINE(items)
    };

    struct Tree {
        std::vector<Tree> children;
        MSGPACK_DEFINE(children)
    };

#ifdef EXTRACHAIN_TEST_TRACK_MALLOC
    bool        track_allocations = false;
    std::size_t largest_request   = 0;
#endif

    template <class Decode>
    void require_small_rejection(Decode decode) {
#ifdef EXTRACHAIN_TEST_TRACK_MALLOC
        largest_request   = 0;
        track_allocations = true;
#endif
        const auto decoded = decode();
#ifdef EXTRACHAIN_TEST_TRACK_MALLOC
        track_allocations = false;
        TEST_REQUIRE(largest_request < 1024 * 1024);
#endif
        TEST_REQUIRE(!decoded.has_value());
    }
} // namespace

#ifdef EXTRACHAIN_TEST_TRACK_MALLOC
extern "C" void *__real_malloc(std::size_t);
extern "C" void *__wrap_malloc(std::size_t size) {
    if (track_allocations) {
        largest_request = std::max(largest_request, size);
        // Keep the regression safe even when the decoder loses its limits.
        if (size > 32ULL * 1024 * 1024) {
            return nullptr;
        }
    }
    return __real_malloc(size);
}
#endif

int main() {
    for (const auto &bytes : { std::string("\xdd\xff\xff\xff\xff", 5), std::string("\xdf\xff\xff\xff\xff", 5) }) {
        require_small_rejection([&] {
            return MessagePack::deserialize<Values>(bytes);
        });
        const std::vector<std::uint8_t> encoded(bytes.begin(), bytes.end());
        require_small_rejection([&] {
            return ExtraChain::Contracts::Codec::decode_json(encoded);
        });
        require_small_rejection([&] {
            return ExtraChain::Contracts::Codec::decode_response(encoded);
        });
        require_small_rejection([&] {
            return ExtraChain::Contracts::Codec::decode_effects(encoded);
        });
    }

    Values values;
    for (int index = 0; index < 4096; ++index) {
        values.items.push_back(index);
    }
    const auto bytes   = MessagePack::serialize(values);
    const auto decoded = MessagePack::deserialize<Values>(bytes, std::numeric_limits<std::size_t>::max());
    TEST_REQUIRE(decoded.has_value());
    TEST_REQUIRE_EQ(decoded.value().items, values.items);
    TEST_REQUIRE(!MessagePack::deserialize<Values>(bytes, bytes.size() - 1).has_value());
    TEST_REQUIRE(!MessagePack::deserialize<Values>(std::string()).has_value());

    for (const auto depth : { 12, 128 }) {
        Tree  tree;
        auto *cursor = &tree;
        for (int level = 0; level < depth; ++level) {
            cursor->children.emplace_back();
            cursor = &cursor->children.back();
        }
        const auto encoded = MessagePack::serialize(tree);
        const auto result  = MessagePack::deserialize<Tree>(encoded);
        TEST_REQUIRE_EQ(result.has_value(), depth == 12);
    }

    for (const auto depth : { 64, 65 }) {
        std::vector<std::uint8_t> encoded(depth, 0x91);
        encoded.push_back(0xc0);
        const auto decoded = ExtraChain::Contracts::Codec::decode_json(encoded);
        TEST_REQUIRE_EQ(decoded.has_value(), depth == 64);
    }
    std::puts("MessagePack allocation limits: PASS");
}
