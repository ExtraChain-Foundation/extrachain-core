#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json.hpp>
#include <msgpack.hpp>

#include "utils/exc_magic.h"
#include "utils/exc_msgpack_describe.h"

namespace MessagePack {
    template <class T>
    std::string serialize(const T &value) {
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, value);
        return std::string(buffer.data(), buffer.size());
    }

    enum class DeserializeError {
        EmptyData,
        DeserializationFailed,
    };

    template <class T, class StringContainer>
    std::expected<T, DeserializeError> deserialize(const StringContainer &data, std::size_t size = 0) {
        const auto available  = static_cast<std::size_t>(data.size());
        const auto input_size = size == 0 ? available : std::min(size, available);
        if (input_size == 0) {
            return std::unexpected(DeserializeError::EmptyData);
        }

        try {
            return msgpack::unpack(data.data(), input_size).get().template as<T>();
        } catch (const std::exception &) {
            return std::unexpected(DeserializeError::DeserializationFailed);
        }
    }

    template <class T>
    std::vector<std::string> serialize_container(const std::vector<T> &list) {
        std::vector<std::string> result;
        result.reserve(list.size());
        for (const auto &item : list) {
            result.push_back(serialize(item));
        }
        return result;
    }

    template <class T>
    std::expected<std::vector<T>, DeserializeError> deserialize_container(
        const std::vector<std::string> &data_container) {
        std::vector<T> result;
        result.reserve(data_container.size());

        for (const auto &data : data_container) {
            const auto element = deserialize<T>(data);
            if (element.has_value()) {
                result.push_back(element.value());
            }
        }

        return result;
    }
} // namespace MessagePack

namespace Json {
    template <typename T>
    boost::json::value serialize_value(const T &value) {
        return json_convert::to_json(value);
    }

    template <typename T>
    std::string serialize(const T &value) {
        return boost::json::serialize(serialize_value(value));
    }

    template <typename T>
    std::expected<T, std::string> deserialize(std::string_view data) {
        try {
            return json_convert::from_json<T>(boost::json::parse(data));
        } catch (const std::exception &error) {
            return std::unexpected(error.what());
        }
    }

    template <typename T>
    std::expected<T, std::string> deserialize(const std::string &data) {
        return deserialize<T>(std::string_view(data));
    }

    template <typename T>
    std::expected<T, std::string> deserialize(const std::vector<std::uint8_t> &data) {
        return deserialize<T>(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()));
    }

    template <typename T>
    std::expected<T, std::string> _no_try_deserialize(std::string_view data) {
        return json_convert::from_json<T>(boost::json::parse(data));
    }
} // namespace Json
