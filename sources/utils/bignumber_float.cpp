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

#include "utils/exc_logs.h"

BigNumberFloat::BigNumberFloat()
    : m_data(0) {
}

BigNumberFloat::BigNumberFloat(const std::string &bigNumberFloat) {
    try {
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

    str.erase(str.find_last_not_of('0') + 1, std::string::npos);
    if (str.back() == '.') {
        str.pop_back();
    }

    return str;
}

std::string BigNumberFloat::to_hex_string() const {
    std::string str     = to_string();
    size_t      dot_pos = str.find('.');

    if (dot_pos == std::string::npos) {
        return BigNumber(str).to_hex_string();
    }

    std::string integer_part    = str.substr(0, dot_pos);
    std::string fractional_part = str.substr(dot_pos + 1);

    size_t size_before = fractional_part.size();
    fractional_part.erase(0, fractional_part.find_first_not_of('0'));
    size_t zeros = size_before - fractional_part.size();

    BigNumber int_bn(integer_part);
    BigNumber frac_bn(fractional_part);
    return int_bn.to_hex_string() + "." + std::string(zeros, '0') + frac_bn.to_hex_string();
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

BigNumberFloat BigNumberFloat::from_hex(const std::string &hex) {
    if (hex.empty()) return BigNumberFloat(0);

    size_t dot_pos = hex.find('.');
    if (dot_pos == std::string::npos) {
        return BigNumberFloat(BigNumber::from_hex(hex));
    }

    std::string integer_part    = hex.substr(0, dot_pos);
    std::string fractional_part = hex.substr(dot_pos + 1);

    size_t size_before = fractional_part.size();
    fractional_part.erase(0, fractional_part.find_first_not_of('0'));
    size_t zeros = size_before - fractional_part.size();

    BigNumber int_bn  = BigNumber::from_hex(integer_part);
    BigNumber frac_bn = BigNumber::from_hex(fractional_part.empty() ? "0" : fractional_part);

    std::string dec_str = int_bn.to_string() + "." + std::string(zeros, '0') + frac_bn.to_string();
    return BigNumberFloat(dec_str);
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
        return value.to_string();
    }

    BigNumberFloat custom_magic<BigNumberFloat>::write(const std::string &value) {
        if (BigNumber::is_hex_string(value)) {
            return BigNumberFloat::from_hex(value);
        }
        return BigNumberFloat(value);
    }
} // namespace magic
