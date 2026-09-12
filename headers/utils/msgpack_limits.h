#pragma once

#include <cstddef>
#include <msgpack.hpp>
#include <msgpack/null_visitor.hpp>
#include <msgpack/parse.hpp>

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

    namespace detail {
        struct StructureBudget : msgpack::null_visitor {
            std::size_t remaining;
            std::size_t container_limit;
            std::size_t depth_limit;
            std::size_t depth = 0;

            bool enter(std::size_t elements, std::size_t slots) {
                if (elements > container_limit || slots > remaining || depth >= depth_limit)
                    return false;
                remaining -= slots;
                ++depth;
                return true;
            }
            bool start_array(std::uint32_t elements) {
                return enter(elements, elements);
            }
            bool start_map(std::uint32_t pairs) {
                return enter(pairs, std::size_t(pairs) * 2);
            }
            bool end_array() {
                --depth;
                return true;
            }
            bool end_map() {
                --depth;
                return true;
            }
        };
    } // namespace detail

    inline bool has_bounded_structure(std::string_view data,
                                      std::size_t      objects,
                                      std::size_t      container_limit,
                                      std::size_t      depth_limit = MaximumNestingDepth) {
        if (objects == 0)
            return false;
        detail::StructureBudget budget { { }, objects - 1, container_limit, depth_limit };
        std::size_t             offset = 0;
        return msgpack::parse(data.data(), data.size(), offset, budget) && offset == data.size();
    }
} // namespace MessagePack
