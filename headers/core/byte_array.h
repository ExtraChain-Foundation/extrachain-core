/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <vector>

#include "utils/exc_utils_base64.h"

class ByteArray {
public:
    template <std::size_t N>
    ByteArray(const std::array<std::uint8_t, N>& value)
        : data_(value.begin(), value.end()) {
    }

    ByteArray(std::vector<std::uint8_t> value)
        : data_(std::move(value)) {
    }

    ByteArray(std::string_view value)
        : data_(reinterpret_cast<const std::uint8_t*>(value.data()),
                reinterpret_cast<const std::uint8_t*>(value.data()) + value.size()) {
    }

    ByteArray(const std::string& value)
        : ByteArray(std::string_view(value)) {
    }

    ByteArray(const char* data, std::size_t length)
        : data_(reinterpret_cast<const std::uint8_t*>(data),
                reinterpret_cast<const std::uint8_t*>(data) + length) {
    }

    explicit ByteArray(const char* data)
        : ByteArray(data, std::strlen(data)) {
    }

    template <std::size_t N>
    [[nodiscard]] std::array<std::uint8_t, N> toArray() const {
        std::array<std::uint8_t, N> result {};
        std::copy_n(data_.begin(), std::min(N, data_.size()), result.begin());
        return result;
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return data_;
    }

    [[nodiscard]] std::vector<std::uint8_t> toBytes() const {
        return data_;
    }

    [[nodiscard]] std::vector<std::uint8_t> toVector() const {
        return data_;
    }

    [[nodiscard]] std::string toString() const {
        return { reinterpret_cast<const char*>(data_.data()), data_.size() };
    }

    [[nodiscard]] std::string toBase64() const {
        return Utils::to_base64(toString());
    }

    [[nodiscard]] ByteArray slice(std::size_t start, std::size_t length) const {
        const auto first = std::min(start, data_.size());
        const auto last  = first + std::min(length, data_.size() - first);
        return ByteArray(std::vector<std::uint8_t>(data_.begin() + first, data_.begin() + last));
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return data_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return data_.empty();
    }

    [[nodiscard]] const std::uint8_t* data() const noexcept {
        return data_.data();
    }

    [[nodiscard]] std::uint8_t* data() noexcept {
        return data_.data();
    }

    auto begin() noexcept {
        return data_.begin();
    }
    auto end() noexcept {
        return data_.end();
    }
    auto begin() const noexcept {
        return data_.begin();
    }
    auto end() const noexcept {
        return data_.end();
    }

    std::uint8_t& operator[](std::size_t index) {
        return data_[index];
    }
    const std::uint8_t& operator[](std::size_t index) const {
        return data_[index];
    }

    bool operator==(const ByteArray&) const = default;

    ByteArray operator+(const ByteArray& other) const {
        auto result = data_;
        result.insert(result.end(), other.data_.begin(), other.data_.end());
        return ByteArray(std::move(result));
    }

    static std::expected<ByteArray, Base64Error> fromBase64(std::string_view encoded) {
        auto decoded = Utils::from_base64(std::string(encoded));
        if (!decoded.has_value()) {
            return std::unexpected(decoded.error());
        }
        return ByteArray(std::move(decoded.value()));
    }

private:
    std::vector<std::uint8_t> data_;
};
