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

#include <exception>
#include <random>

#include "utils/exc_logs.h"

using boost::multiprecision::cpp_int;

BigNumber::BigNumber()
    : m_data(0) {
}

BigNumber::BigNumber(const std::string &bigNumber, NumeralBase base) {
    if (bigNumber == "inf")
        eFatal("BigNumber: infinity");
    try {
        if (bigNumber.empty()) {
            this->m_data = cpp_int(0);
        } else {
            if (base == NumeralBase::Dec) {
                std::string trimmed = bigNumber;
                trimmed.erase(0, trimmed.find_first_not_of('0'));
                this->m_data = cpp_int(trimmed);
            } else {
                std::stringstream ss;
                ss << std::hex << bigNumber;
                ss >> m_data;
            }
        }
    } catch (std::exception &) {
        qDebug() << "Incorrect BigNumber value:" << bigNumber.c_str();
        assert(false);
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
    // other.m_data = boost::multiprecision::cpp_int(0);
}

BigNumber::BigNumber(const cpp_int &number) {
    this->m_data = number;
    UPDATE_DEBUG()
}

BigNumber::BigNumber(int number) {
    this->m_data = cpp_int(number);
    UPDATE_DEBUG()
}

BigNumber::BigNumber(long long number) {
    this->m_data = cpp_int(number);
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

std::string BigNumber::to_string(NumeralBase numSystem) const {
    if (numSystem == NumeralBase::Dec) {
        return m_data.str();
    } else {
        std::stringstream ss;
        if (m_data >= 0) {
            ss << std::hex << m_data;
            return ss.str();
        } else {
            ss << std::hex << boost::multiprecision::abs(m_data);
            return "-" + ss.str();
        }
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

std::expected<BigNumber, BigNumberError> BigNumber::create(const std::string &bigNumber, NumeralBase base) {
    if (bigNumber == "inf") {
        return std::unexpected(BigNumberError::Infinity);
    }

    try {
        BigNumber bn;
        if (bigNumber.empty()) {
            bn.m_data = cpp_int(0);
        } else {
            if (base == NumeralBase::Dec) {
                std::string trimmed = bigNumber;
                trimmed.erase(0, trimmed.find_first_not_of('0'));
                bn.m_data = cpp_int(trimmed);
            } else {
                std::stringstream ss;
                ss << std::hex << bigNumber;
                ss >> bn.m_data;
            }
        }
        return bn;
    } catch (std::exception &) {
        qDebug() << "Incorrect BigNumber value:" << bigNumber.c_str();
        assert(false);
        return std::unexpected(BigNumberError::InvalidNumber);
    }
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
    return value.to_string(NumeralBase::Hex);
}

BigNumber custom_magic<BigNumber>::write(const std::string &value) {
    return BigNumber(value);
}
} // namespace magic
