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

#include "utils/bignumber.h"

#include <algorithm>
#include <exception>
#include <sstream>

#include "utils/exc_logs.h"

using boost::multiprecision::cpp_int;

BigNumber::BigNumber()
    : m_data(0) {
    UPDATE_DEBUG()
}

BigNumber::BigNumber(const std::string &bigNumber) {
    if (bigNumber == "inf")
        eFatal("BigNumber: infinity");
    try {
        if (bigNumber.empty()) {
            this->m_data = cpp_int(0);
        } else if (is_hex_string(bigNumber)) {
            *this = from_hex(bigNumber);
        } else {
            std::string trimmed = bigNumber;
            bool is_negative = !trimmed.empty() && trimmed[0] == '-';
            if (is_negative) trimmed = trimmed.substr(1);
            trimmed.erase(0, trimmed.find_first_not_of('0'));
            if (trimmed.empty()) trimmed = "0";
            this->m_data = cpp_int(trimmed);
            if (is_negative) this->m_data = -this->m_data;
        }
    } catch (std::exception &) {
        eLog("Incorrect BigNumber value: {}", bigNumber);
        this->m_data = -100000;
    }

    UPDATE_DEBUG()
}

BigNumber::BigNumber(const BigNumber &other) {
    this->m_data = other.data();
    UPDATE_DEBUG()
}

BigNumber::BigNumber(BigNumber &&other) noexcept {
    this->m_data = std::move(other.m_data);
    UPDATE_DEBUG()
}

BigNumber::BigNumber(const cpp_int &number) {
    this->m_data = number;
    UPDATE_DEBUG()
}

BigNumber BigNumber::operator&(const BigNumber &value) {
    BigNumber da(m_data & value.data());
    return da;
}

BigNumber BigNumber::operator>>(const uint &value) {
    BigNumber ret(m_data >> value);
    return ret;
}

BigNumber BigNumber::operator>>=(const uint &value) {
    BigNumber ret(m_data >> value);
    m_data = ret.data();
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator+(const BigNumber &bigNumber) const {
    return BigNumber(m_data + bigNumber.data());
}

BigNumber BigNumber::operator+(long long number) const {
    return BigNumber(m_data + number);
}

BigNumber BigNumber::operator-(const BigNumber &bigNumber) const {
    return BigNumber(m_data - bigNumber.data());
}

BigNumber BigNumber::operator-(long long number) const {
    return BigNumber(m_data - number);
}

BigNumber BigNumber::operator*(const BigNumber &bigNumber) const {
    return BigNumber(m_data * bigNumber.data());
}

BigNumber BigNumber::operator*(long long number) const {
    return BigNumber(m_data * number);
}

BigNumber BigNumber::operator/(const BigNumber &bigNumber) const {
    return BigNumber(m_data / bigNumber.data());
}

BigNumber BigNumber::operator/(long long number) const {
    return BigNumber(m_data / number);
}

BigNumber BigNumber::operator%(const BigNumber &bigNumber) const {
    return BigNumber(m_data % bigNumber.data());
}

BigNumber BigNumber::operator%(long long number) const {
    return BigNumber(m_data % number);
}

BigNumber &BigNumber::operator=(const BigNumber &bigNumber) {
    m_data = bigNumber.data();
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator=(long long number) {
    m_data = number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator++() {
    *this = *this + 1;
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator++(int) {
    ++m_data;
    UPDATE_DEBUG()
    return BigNumber(m_data);
}

BigNumber &BigNumber::operator--() {
    m_data--;
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator--(int) {
    --m_data;
    UPDATE_DEBUG()
    return BigNumber(m_data);
}

BigNumber &BigNumber::operator+=(const BigNumber &bigNumber) {
    this->m_data += bigNumber.m_data;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator+=(long long number) {
    this->m_data += number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator-=(const BigNumber &bigNumber) {
    this->m_data -= bigNumber.data();
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator-=(long long number) {
    this->m_data -= number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator*=(const BigNumber &bigNumber) {
    this->m_data *= bigNumber.m_data;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator*=(long long number) {
    this->m_data *= number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator/=(const BigNumber &bigNumber) {
    this->m_data /= bigNumber.m_data;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator/=(long long number) {
    this->m_data /= number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator%=(const BigNumber &bigNumber) {
    this->m_data %= bigNumber.m_data;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator%=(long long number) {
    this->m_data %= number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator-() const {
    return BigNumber(-m_data);
}

const cpp_int &BigNumber::data() const {
    return m_data;
}

std::string BigNumber::to_string() const {
    return m_data.str();
}

std::string BigNumber::to_hex_string() const {
    std::stringstream ss;
    if (m_data >= 0) {
        ss << std::hex << m_data;
        return ss.str();
    } else {
        ss << std::hex << boost::multiprecision::abs(m_data);
        return "-" + ss.str();
    }
}

std::string BigNumber::to_printable_string() const {
    auto res = this->to_string();
    if (res.length() < 6)
        return res;

    std::string result;
    int         counter = 0;
    for (int i = res.length() - 1; i >= 0; i--) {
        result = res[i] + result;
        if (++counter % 3 == 0 && i != 0)
            result = " " + result;
    }
    return result;
}

std::optional<int> BigNumber::to_int() const {
    if (m_data <= std::numeric_limits<int>::max() && m_data >= std::numeric_limits<int>::min()) {
        return static_cast<int>(m_data);
    } else {
        return std::nullopt;
    }
}

BigNumber BigNumber::pow(unsigned long number) {
    auto res = boost::multiprecision::pow(m_data, number);
    return BigNumber(res);
}

BigNumber BigNumber::abs() const {
    auto res = boost::multiprecision::abs(m_data);
    return BigNumber(res);
}

std::expected<BigNumber, BigNumberError> BigNumber::create(const std::string &bigNumber) {
    if (bigNumber == "inf") {
        return std::unexpected(BigNumberError::Infinity);
    }

    try {
        if (bigNumber.empty()) {
            return BigNumber(0);
        }

        std::string trimmed = bigNumber;
        bool is_negative = !trimmed.empty() && trimmed[0] == '-';
        if (is_negative) trimmed = trimmed.substr(1);
        if (trimmed.empty()) {
            return std::unexpected(BigNumberError::InvalidNumber);
        }

        auto all_digits = [](const std::string &s) {
            return std::all_of(s.begin(), s.end(), [](char c) {
                return c >= '0' && c <= '9';
            });
        };
        auto all_hex = [](const std::string &s) {
            return std::all_of(s.begin(), s.end(), [](char c) {
                return (c >= '0' && c <= '9')
                    || (c >= 'a' && c <= 'f')
                    || (c >= 'A' && c <= 'F');
            });
        };

        if (is_hex_string(bigNumber)) {
            if (!all_hex(trimmed)) return std::unexpected(BigNumberError::InvalidNumber);
            return from_hex(bigNumber);
        }

        if (!all_digits(trimmed)) return std::unexpected(BigNumberError::InvalidNumber);
        return BigNumber(bigNumber);
    } catch (std::exception &) {
        eLog("Incorrect BigNumber value: {}", bigNumber);
        return std::unexpected(BigNumberError::InvalidNumber);
    }
}

bool BigNumber::is_hex_string(const std::string &str) {
    if (str.empty()) return false;

    size_t start = 0;
    if (str[0] == '-') start = 1;
    if (start >= str.size()) return false;

    for (size_t i = start; i < str.size(); i++) {
        char c = str[i];
        if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            return true;
        }
    }
    return false;
}

BigNumber BigNumber::from_hex(const std::string &hex) {
    if (hex.empty()) return BigNumber(0);

    std::string trimmed = hex;
    bool is_negative = !trimmed.empty() && trimmed[0] == '-';
    if (is_negative) trimmed = trimmed.substr(1);

    trimmed.erase(0, trimmed.find_first_not_of('0'));
    if (trimmed.empty()) trimmed = "0";

    BigNumber result(boost::multiprecision::cpp_int("0x" + trimmed));
    if (is_negative) {
        result = BigNumber(-result.data());
    }

    return result;
}

std::strong_ordering BigNumber::operator<=>(const int &other) const {
    if (m_data < other)
        return std::strong_ordering::less;
    if (m_data > other)
        return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

std::strong_ordering BigNumber::operator<=>(const BigNumber &other) const {
    if (m_data < other.m_data)
        return std::strong_ordering::less;
    if (m_data > other.m_data)
        return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

bool BigNumber::operator==(const BigNumber &other) const {
    return m_data == other.m_data;
}

bool BigNumber::operator==(const int &other) const {
    return m_data == other;
}

namespace magic {
    std::string custom_magic<BigNumber>::read(const BigNumber &value) {
        return value.to_string();
    }

    BigNumber custom_magic<BigNumber>::write(const std::string &value) {
        if (BigNumber::is_hex_string(value)) {
            return BigNumber::from_hex(value);
        }
        return BigNumber(value);
    }
} // namespace magic
