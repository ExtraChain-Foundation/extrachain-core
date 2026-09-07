#pragma once

#include <cstddef>
#include <msgpack.hpp>

namespace MessagePack {
    inline constexpr std::size_t MaximumNestingDepth = 64;

    inline msgpack::unpack_limit unpack_limits(std::size_t input_size) {
        // Reject impossible counts before MessagePack allocates its object arrays.
        return msgpack::unpack_limit(input_size,
                                     input_size / 2,
                                     input_size,
                                     input_size,
                                     input_size,
                                     MaximumNestingDepth);
    }
} // namespace MessagePack
