/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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
#include <boost/mp11.hpp>
#include <boost/core/demangle.hpp>
#include <boost/json.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <QDebug>
#include <type_traits>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <memory>
#include "magic_enum.hpp"
#include "utils/exc_utils_base64.h"
// #include "utils/exc_logs.h"

template <>
struct fmt::formatter<boost::json::object> : fmt::ostream_formatter { };
template <>
struct fmt::formatter<boost::json::array> : fmt::ostream_formatter { };
template <>
struct fmt::formatter<boost::json::value> : fmt::ostream_formatter { };

namespace magic {
// Forward declarations
template <typename T>
std::string magic(const T& obj);

// Type traits
template <typename T>
struct is_optional : std::false_type { };

template <typename T>
struct is_optional<std::optional<T>> : std::true_type { };

template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
struct is_shared_ptr : std::false_type { };

template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type { };

template <typename T>
inline constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

template <typename T>
struct is_unique_ptr : std::false_type { };

template <typename T>
struct is_unique_ptr<std::unique_ptr<T>> : std::true_type { };

template <typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

template <typename T>
struct is_smart_ptr : std::bool_constant<is_shared_ptr_v<T> || is_unique_ptr_v<T>> { };

template <typename T>
inline constexpr bool is_smart_ptr_v = is_smart_ptr<T>::value;

template <typename T, typename = void>
struct is_container : std::false_type { };

template <typename T>
struct is_container<
    T,
    std::void_t<typename T::iterator, typename T::const_iterator, decltype(std::declval<T>().size())>>
    : std::true_type { };

template <typename T, typename = void>
struct has_value_type : std::false_type { };

template <typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type { };

template <typename T>
struct is_uint8_array : std::false_type { };

template <std::size_t N>
struct is_uint8_array<std::array<uint8_t, N>> : std::true_type { };

template <typename T>
struct is_std_array : std::false_type { };

template <typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : std::true_type { };

template <typename T, typename = void>
struct is_associative_container : std::false_type { };

template <typename T>
struct is_associative_container<T, std::void_t<typename T::key_type, typename T::mapped_type>>
    : std::true_type { };

// Custom magic interface
struct custom_magic_tag { };

template <typename T, typename = void>
struct custom_magic {
    static std::string read(const T& value) {
        if constexpr (boost::describe::has_describe_members<T>::value) {
            return magic(value);
        } else if constexpr (is_optional<T>::value) {
            if (!value.has_value()) {
                return "null";
            }
            return read(value.value());
        } else if constexpr (is_smart_ptr_v<T>) {
            if (!value) {
                return "null";
            }
            return read(*value);
        } else {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        }
    }
};

template <>
struct custom_magic<std::vector<uint8_t>>;

// Member access
template <typename T, typename M>
auto& invoke_member(const T& obj, M member) {
    if constexpr (std::is_member_function_pointer_v<M>) {
        return (obj.*member)();
    } else {
        return obj.*member;
    }
}

// String conversion helpers
namespace detail {
    inline std::string clean_type_name(const std::string& name) {
        std::string result = name;

        size_t pos = result.find("class ");
        while (pos != std::string::npos) {
            result.erase(pos, 6);
            pos = result.find("class ");
        }

        pos = result.find("struct ");
        while (pos != std::string::npos) {
            result.erase(pos, 7);
            pos = result.find("struct ");
        }

        return result;
    }

    inline std::string clean_field_name(const char* name) {
        std::string result = name;

        if (result.length() > 2 && result.substr(0, 2) == "m_") {
            result.erase(0, 2);
        }

        return result;
    }

    template <typename T>
    std::string to_string(const T& value) {
        if constexpr (is_unique_ptr_v<T>) {
            if (!value)
                return "null";
            return to_string(*value);
        } else if constexpr (is_shared_ptr_v<T>) {
            if (!value)
                return "null";
            return to_string(*value);
        } else if constexpr (is_optional<T>::value) {
            if (!value.has_value()) {
                return "null";
            }
            return to_string(value.value());
        } else if constexpr (std::is_same_v<T, std::string>) {
            return '"' + value + '"';
        } else if constexpr (std::is_enum_v<T>) {
            if constexpr (std::is_scoped_enum_v<T>) {
                return std::string(magic_enum::enum_type_name<T>())
                       + "::" + std::string(magic_enum::enum_name(value));
            }
            return std::to_string(static_cast<std::underlying_type_t<T>>(value));
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(value);
        } else if constexpr (is_uint8_array<T>::value) {
            std::string raw(reinterpret_cast<const char*>(value.data()), value.size());
            auto        is_empty = std::ranges::all_of(raw, [&value](const auto& x) {
                return x == '\0';
            });
            return '"' + (is_empty ? "empty" : Utils::to_base64(raw)) + '"';
        } else if constexpr (is_container<T>::value) {
            if constexpr (is_associative_container<T>::value) {
                std::string result = "{ ";
                bool        first  = true;
                for (const auto& pair : value) {
                    if (!first)
                        result += ", ";
                    result += to_string(pair.first) + ": " + to_string(pair.second);
                    first = false;
                }
                return result + " }";
            } else {
                std::string result = "[ ";
                bool        first  = true;
                for (const auto& item : value) {
                    if (!first)
                        result += ", ";
                    result += to_string(item);
                    first = false;
                }
                return result + " ]";
            }
        } else {
            return custom_magic<T>::read(value);
        }
    }
}

// Main magic implementation
template <typename T>
std::string magic(const T& obj) {
    if constexpr (boost::describe::has_describe_members<T>::value) {
        std::string result = detail::clean_type_name(boost::core::demangle(typeid(T).name()));
        result += " { ";
        bool first = true;

        boost::mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>(
            [&](auto D) {
                if constexpr (!std::is_same_v<decltype(D), custom_magic_tag>) {
                    if (!first)
                        result += ", ";
                    result += detail::clean_field_name(D.name) + ": "
                              + detail::to_string(invoke_member(obj, D.pointer));
                    first = false;
                }
            });

        return result + " }";
    } else {
        return detail::to_string(obj);
    }
}

} // namespace magic

// JSON conversion namespace
namespace json_convert {
template <typename T, typename = void>
struct has_custom_magic : std::false_type { };

template <typename T>
struct has_custom_magic<
    T,
    std::void_t<
        decltype(magic::custom_magic<T>::read(std::declval<T>())),
        decltype(magic::custom_magic<T>::write(std::declval<std::string>()))>> : std::true_type { };

template <typename T>
inline constexpr bool has_custom_magic_v = has_custom_magic<T>::value;

template <typename T>
boost::json::value to_json(const T& obj);

template <typename T>
T from_json(const boost::json::value& json);

namespace detail {
    template <std::size_t N>
    boost::json::value array_uint8_to_json(const std::array<uint8_t, N>& arr) {
        std::string raw(reinterpret_cast<const char*>(arr.data()), N);
        return boost::json::value(Utils::to_base64(raw));
    }

    template <std::size_t N>
    std::array<uint8_t, N> array_uint8_from_json(const boost::json::value& json) {
        std::string            decoded = Utils::from_base64<std::string>(json.as_string().c_str());
        std::array<uint8_t, N> result {};
        std::size_t            copy_size = std::min<std::size_t>(N, decoded.size());
        std::memcpy(result.data(), decoded.data(), copy_size);
        return result;
    }

    template <typename T>
    boost::json::value to_json_impl(const T& obj) {
        if constexpr (magic::is_unique_ptr_v<T>) {
            if (!obj)
                return boost::json::value(nullptr);
            return to_json(*obj);
        } else if constexpr (magic::is_shared_ptr_v<T>) {
            if (!obj)
                return boost::json::value(nullptr);
            return to_json(*obj);
        } else if constexpr (magic::is_optional<T>::value) {
            if (!obj.has_value())
                return boost::json::value(nullptr);
            return to_json(obj.value());
        } else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
            return boost::json::value(obj);
        } else if constexpr (std::is_enum_v<T>) {
            return boost::json::value(static_cast<std::underlying_type_t<T>>(obj));
        } else if constexpr (magic::is_uint8_array<T>::value) {
            return array_uint8_to_json(obj);
        } else if constexpr (magic::is_container<T>::value) {
            if constexpr (magic::is_associative_container<T>::value) {
                boost::json::object result;
                for (const auto& pair : obj) {
                    auto value = to_json(pair.second);
                    if (!value.is_null()) {
                        result[to_json(pair.first).as_string()] = value;
                    }
                }
                return result;
            } else {
                boost::json::array result;
                for (const auto& item : obj) {
                    auto value = to_json(item);
                    if (!value.is_null()) {
                        result.push_back(value);
                    }
                }
                return result;
            }
        } else if constexpr (boost::describe::has_describe_members<T>::value) {
            boost::json::object result;
            boost::mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>(
                [&](auto D) {
                    if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                        auto value = to_json(magic::invoke_member(obj, D.pointer));
                        if (!value.is_null()) {
                            result[magic::detail::clean_field_name(D.name)] = value;
                        }
                    }
                });
            return result;
        } else if constexpr (has_custom_magic_v<T>) {
            return boost::json::value(magic::custom_magic<T>::read(obj));
        } else {
            return boost::json::value();
        }
    }

    template <typename T>
    T from_json_impl(const boost::json::value& json) {
        // eInfo("JSON: {} {} {}", typeid(T).name(), json, std::string(magic_enum::enum_name(json.kind())));

        if constexpr (magic::is_shared_ptr_v<T>) {
            if (json.is_null()) {
                return nullptr;
            }
            return std::make_shared<typename T::element_type>(from_json<typename T::element_type>(json));
        } else if constexpr (magic::is_unique_ptr_v<T>) {
            if (json.is_null())
                return nullptr;
            return std::make_unique<typename T::element_type>(from_json<typename T::element_type>(json));
        } else if constexpr (magic::is_optional<T>::value) {
            if (json.is_null()) {
                return std::nullopt;
            }
            return std::make_optional(from_json<typename T::value_type>(json));
        } else if constexpr (std::is_arithmetic_v<T>) {
            if constexpr (std::is_integral_v<T>) {
                if (json.is_bool())
                    return json.as_bool();
                return json.is_uint64() ? json.as_uint64() : json.as_int64();
            }
            return json.as_double();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return json.is_null() ? "" : json.as_string().c_str();
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(
                json.is_string() ? std::stoll(std::string(json.as_string())) : json.as_int64());
        } else if constexpr (magic::is_uint8_array<T>::value) {
            return array_uint8_from_json<std::tuple_size_v<T>>(json);
        } else if constexpr (magic::is_container<T>::value) {
            T result;
            if constexpr (magic::is_associative_container<T>::value) {
                const auto& obj = json.as_object();
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    result.insert({ from_json<typename T::key_type>(boost::json::value(it->key())),
                                    from_json<typename T::mapped_type>(it->value()) });
                }
            } else {
                const auto& arr = json.as_array();
                if constexpr (magic::is_std_array<T>::value) {
                    // Handle std::array specifically
                    std::size_t i = 0;
                    for (const auto& item : arr) {
                        if (i < std::tuple_size<T>::value) {
                            result[i++] = from_json<typename T::value_type>(item);
                        }
                    }
                } else {
                    // Handle other containers
                    for (const auto& item : arr) {
                        result.insert(result.end(), from_json<typename T::value_type>(item));
                    }
                }
            }
            return result;
        } else if constexpr (boost::describe::has_describe_members<T>::value) {
            T           result {};
            const auto& obj = json.as_object();
            boost::mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>(
                [&](auto D) {
                    if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                        auto it = obj.find(magic::detail::clean_field_name(D.name));
                        if (it != obj.end()) {
                            // eInfo("JSON: Parsing for {}", D.name);

                            using MemberType  = std::remove_reference_t<decltype(result.*D.pointer)>;
                            result.*D.pointer = from_json<MemberType>(it->value());
                        }
                    }
                });
            return result;
        } else if constexpr (has_custom_magic_v<T>) {
            return magic::custom_magic<T>::write(json.as_string().c_str());
        } else {
            return T {};
        }
    }
}

template <typename T>
boost::json::value to_json(const T& obj) {
    return detail::to_json_impl(obj);
}

template <typename T>
T from_json(const boost::json::value& json) {
    return detail::from_json_impl<T>(json);
}

} // namespace json_convert

// Macros for adding magic support
#define MAKE_MAGICAL_OPERATORS(ClassName)                                                                    \
    inline std::ostream& operator<<(std::ostream& os, const ClassName& obj) {                                \
        return os << magic::magic(obj);                                                                      \
    }                                                                                                        \
    inline QDebug operator<<(QDebug debug, const ClassName& obj) {                                           \
        return debug << magic::magic(obj).c_str();                                                           \
    }

#define MAKE_MAGICAL(ClassName) MAKE_MAGICAL_OPERATORS(ClassName)

#define MAKE_CUSTOM_MAGICAL(ClassName)                                                                       \
    namespace magic {                                                                                        \
        template <>                                                                                          \
        struct custom_magic<ClassName> {                                                                     \
            static std::string read(const ClassName& value);                                                 \
            static ClassName   write(const std::string& value);                                              \
        };                                                                                                   \
    }                                                                                                        \
    MAKE_MAGICAL(ClassName)

template <typename T>
struct fmt::formatter<
    T,
    std::enable_if_t<
        boost::describe::has_describe_members<T>::value || json_convert::has_custom_magic_v<T>,
        char>> {
    constexpr auto parse(format_parse_context& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const T& obj, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", magic::magic(obj));
    }
};
