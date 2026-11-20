/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <boost/describe.hpp>
#include <msgpack.hpp>

namespace msgpack_describe {

    template <typename Stream, typename T>
    void pack_described(msgpack::packer<Stream>& packer, const T& value) {
        using members =
            boost::describe::describe_members<T, boost::describe::mod_any_access | boost::describe::mod_inherited>;
        constexpr size_t N = boost::mp11::mp_size<members>::value;
        packer.pack_array(N);
        boost::mp11::mp_for_each<members>([&](auto D) {
            if constexpr (std::is_enum_v<std::remove_reference_t<decltype(value.*D.pointer)>>) {
                packer.pack(std::to_underlying(value.*D.pointer));
            } else {
                packer.pack(value.*D.pointer);
            }
        });
    }

    template <typename T>
    void unpack_described(const msgpack::object& obj, T& value) {
        using members =
            boost::describe::describe_members<T, boost::describe::mod_any_access | boost::describe::mod_inherited>;
        constexpr size_t N = boost::mp11::mp_size<members>::value;
        if (obj.type != msgpack::type::ARRAY || obj.via.array.size < N) {
            throw msgpack::type_error();
        }
        size_t idx = 0;
        boost::mp11::mp_for_each<members>([&](auto D) {
            if (idx < obj.via.array.size) {
                const auto& item  = obj.via.array.ptr[idx++];
                using member_type = std::remove_reference_t<decltype(value.*D.pointer)>;
                if constexpr (std::is_enum_v<member_type>) {
                    std::underlying_type_t<member_type> temp;
                    item.convert(temp);
                    value.*D.pointer = static_cast<member_type>(temp);
                } else {
                    member_type temp;
                    item.convert(temp);
                    value.*D.pointer = std::move(temp);
                }
            }
        });
    }
} // namespace msgpack_describe

namespace msgpack {
    MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
        namespace adaptor {
            template <typename T>
            struct pack<T, typename std::enable_if<boost::describe::has_describe_members<T>::value>::type> {
                template <typename Stream>
                msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const T& v) const {
                    msgpack_describe::pack_described(o, v);
                    return o;
                }
            };

            template <typename T>
            struct convert<T, typename std::enable_if<boost::describe::has_describe_members<T>::value>::type> {
                msgpack::object const& operator()(msgpack::object const& o, T& v) const {
                    msgpack_describe::unpack_described(o, v);
                    return o;
                }
            };
        } // namespace adaptor
    }
} // namespace msgpack
