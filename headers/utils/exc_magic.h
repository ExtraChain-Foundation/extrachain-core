#ifndef UNIVERSAL_MAGIC_HPP
#define UNIVERSAL_MAGIC_HPP

#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <boost/core/demangle.hpp>
#include <boost/json.hpp>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <QDebug>
#include <type_traits>
#include <string>

#include "magic_enum.hpp"

namespace magic {

// Forward declarations
template <typename T>
std::string magic(const T& obj);

// Type traits
template <typename T, typename = void>
struct is_container : std::false_type { };

template <typename T>
struct is_container<
    T,
    std::void_t<typename T::iterator, typename T::const_iterator, decltype(std::declval<T>().size())>>
    : std::true_type { };

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
        } else {
            std::ostringstream oss;
            oss << value;
            return oss.str();
        }
    }
};

// Member access
template <typename T, typename M>
auto invoke_member(const T& obj, M member) {
    if constexpr (std::is_member_function_pointer_v<M>) {
        return (obj.*member)();
    } else {
        return obj.*member;
    }
}

// String conversion helpers
namespace detail {
    template <typename T>
    std::string to_string(const T& value) {
        if constexpr (std::is_same_v<T, std::string>) {
            return '"' + value + '"';
        } else if constexpr (std::is_enum_v<T>) {
            if constexpr (std::is_scoped_enum_v<T>) {
                return std::string(magic_enum::enum_type_name<T>())
                       + "::" + std::string(magic_enum::enum_name(value));
            }
            return std::to_string(static_cast<std::underlying_type_t<T>>(value));
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(value);
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
        std::string result = boost::core::demangle(typeid(T).name());
        result += " { ";
        bool first = true;

        boost::mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>(
            [&](auto D) {
                if constexpr (!std::is_same_v<decltype(D), custom_magic_tag>) {
                    if (!first)
                        result += ", ";
                    result += std::string(D.name) + ": " + detail::to_string(invoke_member(obj, D.pointer));
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
    template <typename T>
    boost::json::value to_json_impl(const T& obj) {
        if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
            return boost::json::value(obj);
        } else if constexpr (std::is_enum_v<T>) {
            return boost::json::value(static_cast<std::underlying_type_t<T>>(obj));
        } else if constexpr (magic::is_container<T>::value) {
            if constexpr (magic::is_associative_container<T>::value) {
                boost::json::object result;
                for (const auto& pair : obj) {
                    result[to_json(pair.first).as_string()] = to_json(pair.second);
                }
                return result;
            } else {
                boost::json::array result;
                for (const auto& item : obj) {
                    result.push_back(to_json(item));
                }
                return result;
            }
        } else if constexpr (boost::describe::has_describe_members<T>::value) {
            boost::json::object result;
            boost::mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>(
                [&](auto D) {
                    if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                        result[D.name] = to_json(magic::invoke_member(obj, D.pointer));
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
        if constexpr (std::is_arithmetic_v<T>) {
            if constexpr (std::is_integral_v<T>) {
                return static_cast<T>(json.as_int64());
            } else {
                return static_cast<T>(json.as_double());
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            return json.as_string().c_str();
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<T>(json.as_int64());
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
                for (const auto& item : arr) {
                    result.insert(result.end(), from_json<typename T::value_type>(item));
                }
            }
            return result;
        } else if constexpr (boost::describe::has_describe_members<T>::value) {
            T           result {};
            const auto& obj = json.as_object();
            boost::mp11::mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>(
                [&](auto D) {
                    if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                        auto it = obj.find(D.name);
                        if (it != obj.end()) {
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
#define MAKE_MAGICAL(ClassName)                                                                              \
    inline std::ostream& operator<<(std::ostream& os, const ClassName& obj) {                                \
        return os << magic::magic(obj);                                                                      \
    }                                                                                                        \
    inline QDebug operator<<(QDebug debug, const ClassName& obj) {                                           \
        return debug.nospace() << magic::magic(obj).c_str();                                                 \
    }                                                                                                        \
    template <>                                                                                              \
    struct fmt::formatter<ClassName> : fmt::ostream_formatter { };

#define MAKE_CUSTOM_MAGICAL(ClassName)                                                                       \
    namespace magic {                                                                                        \
        template <>                                                                                          \
        struct custom_magic<ClassName> {                                                                     \
            static std::string read(const ClassName& value);                                                 \
            static ClassName   write(const std::string& value);                                              \
        };                                                                                                   \
    }                                                                                                        \
    MAKE_MAGICAL(ClassName)

#endif // UNIVERSAL_MAGIC_HPP
