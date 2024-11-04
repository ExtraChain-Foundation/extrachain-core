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

#include "utils/bignumber_float.h"
#include "utils/bignumber.h"
#include <exception>

BigNumberFloat::BigNumberFloat()
    : m_data(0) {
}

BigNumberFloat::BigNumberFloat(const std::string &bigNumberFloat, NumeralBase base) {
    try {
        if (bigNumberFloat.empty()) {
            this->m_data = cpp_dec_float_exc(0);
        } else {
            if (base == NumeralBase::Dec) {
                this->m_data = cpp_dec_float_exc(bigNumberFloat);
            } else {
                *this = fromHex(bigNumberFloat);
            }
        }
    } catch (std::exception &) {
        qDebug() << "Incorrect BigNumberFloat value:" << bigNumberFloat.c_str();
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
    if (bigNumberFloat == 0)
        qFatal("BigNumberFloat: Division by zero");
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

bool BigNumberFloat::isEmpty() const {
    return m_data == 0;
}

QByteArray BigNumberFloat::toByteArray(NumeralBase numSystem) const {
    auto res = toStdString(numSystem);
    return QByteArray::fromStdString(res);
}

std::string BigNumberFloat::toStdString(NumeralBase numSystem) const {
    if (numSystem == NumeralBase::Dec) {
        std::stringstream ss;
        ss << std::setprecision(float_size) << std::fixed << m_data;
        std::string str = ss.str();

        str.erase(str.find_last_not_of('0') + 1, std::string::npos);
        if (str.back() == '.') {
            str.pop_back();
        }

        return str;
    } else if (numSystem == NumeralBase::Hex) {
        std::string str     = toStdString(NumeralBase::Dec);
        size_t      dot_pos = str.find('.');
        if (dot_pos == std::string::npos)
            return BigNumber(str, NumeralBase::Dec).toStdString(NumeralBase::Hex);

        std::string integer_part    = str.substr(0, dot_pos);
        std::string fractional_part = str.substr(dot_pos + 1);

        size_t sizeBefore = fractional_part.size();
        fractional_part.erase(0, fractional_part.find_first_not_of('0'));
        size_t zeros = sizeBefore - fractional_part.size();

        BigNumber one(integer_part, NumeralBase::Dec);
        BigNumber two(fractional_part, NumeralBase::Dec);
        return one.toStdString(NumeralBase::Hex) + "." + std::string(zeros, '0')
               + two.toStdString(NumeralBase::Hex);
    } else {
        throw std::invalid_argument("Unsupported base");
    }
}

QByteArray BigNumberFloat::toZeroByteArray(int size) const {
    auto number = this->toByteArray();
    if (size <= number.length())
        return number;

    auto zero = QByteArray().fill('0', size - number.length());
    return zero + number;
}

BigNumberFloat BigNumberFloat::pow(unsigned long number) {
    auto res = boost::multiprecision::pow(m_data, number);
    return BigNumberFloat(res);
}

BigNumberFloat BigNumberFloat::abs() const {
    auto res = boost::multiprecision::abs(m_data);
    return BigNumberFloat(res);
}

std::expected<BigNumberFloat, BigNumberError>
BigNumberFloat::create(const std::string &bigNumberFloat, NumeralBase base) {
    if (bigNumberFloat == "inf") {
        return std::unexpected(BigNumberError::Infinity);
    }

    try {
        BigNumberFloat bn;
        if (bigNumberFloat.empty()) {
            bn.m_data = cpp_dec_float_exc(0);
        } else {
            if (base == NumeralBase::Dec) {
                bn.m_data = cpp_dec_float_exc(bigNumberFloat);
            } else {
                bn = fromHex(bigNumberFloat);
            }
        }
        return bn;
    } catch (std::exception &) {
        return std::unexpected(BigNumberError::InvalidNumber);
    }
}

BigNumberFloat BigNumberFloat::random(int n, bool zeroAllowed) {
    QByteArray str;
    str.resize(n);
    str[0] = '0';

    while (str[0] == '0')
        str[0] = BigNumberUtils::Chars[QRandomGenerator::global()->bounded(16)];

    for (int i = 1; i != n; ++i)
        str[i] = BigNumberUtils::Chars[QRandomGenerator::global()->bounded(16)];

    BigNumberFloat res(str.toStdString());
    if (!zeroAllowed && res == 0)
        return random(n, zeroAllowed);
    return res;
}

BigNumberFloat BigNumberFloat::random(int n, const BigNumberFloat &max, bool zeroAllowed) {
    if (max.toByteArray(NumeralBase::Hex).length() < n)
        return BigNumberFloat(0);

    BigNumberFloat result;

    do {
        result = random(n, zeroAllowed);
    } while (result >= max);
    return result;
}

BigNumberFloat BigNumberFloat::random(BigNumberFloat max, bool zeroAllowed) {
    QByteArray maxdata = max.toByteArray();
    QByteArray b;
    b.clear();
    b.fill('f', maxdata.size());
    BigNumberFloat t(b.toStdString());

    while (t >= max) {
        int        size = QRandomGenerator::global()->bounded(1, max.toByteArray().size());
        QByteArray res;
        res.clear();
        for (int i = 0; i < size; i++) {
            res.append(BigNumberUtils::Chars[QRandomGenerator::global()->bounded(0, 15)]);
        }
        t = BigNumberFloat(res.toStdString());
    }
    if (!zeroAllowed && t == 0)
        return random(max, zeroAllowed);
    return t;
}

BigNumberFloat BigNumberFloat::fromHex(const std::string &number) {
    size_t dot_pos = number.find('.');
    if (dot_pos == std::string::npos) {
        return BigNumberFloat(BigNumber(number));
    }

    std::string integer_part    = number.substr(0, dot_pos);
    std::string fractional_part = number.substr(dot_pos + 1);
    size_t      sizeBefore      = fractional_part.size();
    fractional_part.erase(0, fractional_part.find_first_not_of('0'));
    size_t    zeros = sizeBefore - fractional_part.size();
    BigNumber one(integer_part, NumeralBase::Hex);
    BigNumber two(fractional_part, NumeralBase::Hex);
    return BigNumberFloat(
        one.toStdString(NumeralBase::Dec) + "." + std::string(zeros, '0') + two.toStdString(NumeralBase::Dec),
        NumeralBase::Dec);
}

namespace magic {
std::string custom_magic<BigNumberFloat>::read(const BigNumberFloat &value) {
    return value.toStdString();
}

BigNumberFloat custom_magic<BigNumberFloat>::write(const std::string &value) {
    return BigNumberFloat(value);
}
} // namespace magic
