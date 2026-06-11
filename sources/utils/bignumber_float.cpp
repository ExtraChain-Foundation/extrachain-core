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

#include "utils/bignumber_float.h"
#include "utils/bignumber.h"

#include <exception>
#include <iomanip>
#include <sstream>

#include "utils/exc_logs.h"

#ifdef QT_DEBUG
    #define UPDATE_DEBUG() qdata = to_string();
#else
    #define UPDATE_DEBUG()
#endif

BigNumberFloat::BigNumberFloat()
    : m_data(0) {
}

BigNumberFloat::BigNumberFloat(const std::string &bigNumberFloat) {
    try {
        // Strict decimal — never sniff hex from content (it is ambiguous).
        // Hex inputs go through from_hex() or a WireFormat::Mode::Legacy scope.
        if (bigNumberFloat.empty()) {
            this->m_data = cpp_dec_float_exc(0);
        } else {
            this->m_data = cpp_dec_float_exc(bigNumberFloat);
        }
    } catch (std::exception &) {
        eLog("Incorrect BigNumberFloat value: {}", bigNumberFloat);
        assert(false);
    }

    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(const BigNumberFloat &other) {
    this->m_data = other.data();
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(BigNumberFloat &&other) noexcept {
    this->m_data = std::move(other.m_data);
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(const BigNumber &other) {
    this->m_data = cpp_dec_float_exc(other.data());
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(const cpp_dec_float_exc &number) {
    this->m_data = number;
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(int number) {
    this->m_data = cpp_dec_float_exc(number);
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(long long number) {
    this->m_data = cpp_dec_float_exc(number);
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(std::uint64_t number) {
    this->m_data = cpp_dec_float_exc(number);
    UPDATE_DEBUG()
}

BigNumberFloat BigNumberFloat::operator+(const BigNumberFloat &bigNumberFloat) const {
    return BigNumberFloat(m_data + bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator+(long long number) const {
    return BigNumberFloat(m_data + number);
}

BigNumberFloat BigNumberFloat::operator-(const BigNumberFloat &bigNumberFloat) const {
    return BigNumberFloat(m_data - bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator-(long long number) const {
    return BigNumberFloat(m_data - number);
}

BigNumberFloat BigNumberFloat::operator*(const BigNumberFloat &bigNumberFloat) const {
    return BigNumberFloat(m_data * bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator*(long long number) const {
    return BigNumberFloat(m_data * number);
}

BigNumberFloat BigNumberFloat::operator/(const BigNumberFloat &bigNumberFloat) const {
    if (bigNumberFloat == 0) {
        eFatal("BigNumberFloat: Division by zero");
    }

    return BigNumberFloat(m_data / bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator/(long long number) const {
    return BigNumberFloat(m_data / number);
}

BigNumberFloat &BigNumberFloat::operator=(const BigNumberFloat &bigNumberFloat) {
    m_data = bigNumberFloat.data();
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator=(long long number) {
    m_data = number;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator++() {
    *this = *this + 1;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat BigNumberFloat::operator++(int) {
    ++m_data;
    UPDATE_DEBUG()
    return BigNumberFloat(m_data);
}

BigNumberFloat &BigNumberFloat::operator--() {
    m_data--;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat BigNumberFloat::operator--(int) {
    --m_data;
    UPDATE_DEBUG()
    return BigNumberFloat(m_data);
}

BigNumberFloat &BigNumberFloat::operator+=(const BigNumberFloat &bigNumberFloat) {
    this->m_data += bigNumberFloat.m_data;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator+=(long long number) {
    this->m_data += number;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator-=(const BigNumberFloat &bigNumberFloat) {
    this->m_data -= bigNumberFloat.data();
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator-=(long long number) {
    this->m_data -= number;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator*=(const BigNumberFloat &bigNumberFloat) {
    this->m_data *= bigNumberFloat.m_data;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator*=(long long number) {
    this->m_data *= number;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator/=(const BigNumberFloat &bigNumberFloat) {
    this->m_data /= bigNumberFloat.m_data;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat &BigNumberFloat::operator/=(long long number) {
    this->m_data /= number;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat BigNumberFloat::operator-() const {
    return BigNumberFloat(-m_data);
}

const cpp_dec_float_exc &BigNumberFloat::data() const {
    return m_data;
}

std::string BigNumberFloat::to_string() const {
    std::stringstream ss;
    ss << std::setprecision(float_size) << std::fixed << m_data;
    std::string str = ss.str();

    size_t dot_pos = str.find('.');
    if (dot_pos != std::string::npos) {
        str.erase(str.find_last_not_of('0') + 1, std::string::npos);
        if (str.back() == '.') {
            str.pop_back();
        }
    }

    return str;
}

std::string BigNumberFloat::to_hex_string() const {
    std::string str     = to_string();
    size_t      dot_pos = str.find('.');
    if (dot_pos == std::string::npos)
        return BigNumber(str).to_hex_string();

    std::string integer_part    = str.substr(0, dot_pos);
    std::string fractional_part = str.substr(dot_pos + 1);

    size_t sizeBefore = fractional_part.size();
    fractional_part.erase(0, fractional_part.find_first_not_of('0'));
    size_t zeros = sizeBefore - fractional_part.size();

    BigNumber one(integer_part);
    BigNumber two(fractional_part);
    return one.to_hex_string() + "." + std::string(zeros, '0') + two.to_hex_string();
}

BigNumberFloat BigNumberFloat::pow(unsigned long number) {
    auto res = boost::multiprecision::pow(m_data, number);
    return BigNumberFloat(res);
}

BigNumberFloat BigNumberFloat::abs() const {
    auto res = boost::multiprecision::abs(m_data);
    return BigNumberFloat(res);
}

std::expected<BigNumberFloat, BigNumberError> BigNumberFloat::create(const std::string &bigNumberFloat) {
    if (bigNumberFloat == "inf") {
        return std::unexpected(BigNumberError::Infinity);
    }

    try {
        return BigNumberFloat(bigNumberFloat);
    } catch (std::exception &) {
        return std::unexpected(BigNumberError::InvalidNumber);
    }
}

BigNumberFloat BigNumberFloat::from_hex(const std::string &number) {
    size_t dot_pos = number.find('.');
    if (dot_pos == std::string::npos) {
        return BigNumberFloat(BigNumber::from_hex(number));
    }

    std::string integer_part    = number.substr(0, dot_pos);
    std::string fractional_part = number.substr(dot_pos + 1);
    size_t      sizeBefore      = fractional_part.size();
    fractional_part.erase(0, fractional_part.find_first_not_of('0'));
    size_t    zeros = sizeBefore - fractional_part.size();
    BigNumber one   = BigNumber::from_hex(integer_part);
    BigNumber two   = BigNumber::from_hex(fractional_part);
    return BigNumberFloat(one.to_string() + "." + std::string(zeros, '0') + two.to_string());
}

void BigNumberFloat::truncate(int decimalPlaces) {
    auto   str    = this->to_string();
    size_t dotPos = str.find('.');

    if (dotPos == std::string::npos) {
        return;
    }

    if (str.length() > dotPos + decimalPlaces + 1) {
        *this = BigNumberFloat(str.substr(0, dotPos + decimalPlaces + 1));
    }
}

std::strong_ordering BigNumberFloat::operator<=>(const int &other) const {
    if (m_data < other)
        return std::strong_ordering::less;
    if (m_data > other)
        return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

bool BigNumberFloat::operator==(const int &other) const {
    return m_data == other;
}

bool BigNumberFloat::operator!=(const int &other) const {
    return !(m_data == other);
}

bool BigNumberFloat::operator==(const BigNumberFloat &other) const {
    return m_data == other.m_data;
}

std::strong_ordering BigNumberFloat::operator<=>(const BigNumberFloat &other) const {
    if (m_data < other.m_data)
        return std::strong_ordering::less;
    if (m_data > other.m_data)
        return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

bool BigNumberFloat::operator!=(const BigNumberFloat &other) const {
    return !(m_data == other.m_data);
}

namespace magic {
    std::string custom_magic<BigNumberFloat>::read(const BigNumberFloat &value) {
        return (WireFormat::get_mode() == WireFormat::Mode::Legacy) ? value.to_hex_string()
                                                                    : value.to_string();
    }

    BigNumberFloat custom_magic<BigNumberFloat>::write(const std::string &value) {
        // Format is decided by the active WireFormat scope, never sniffed.
        return (WireFormat::get_mode() == WireFormat::Mode::Legacy) ? BigNumberFloat::from_hex(value)
                                                                    : BigNumberFloat(value);
    }
} // namespace magic
