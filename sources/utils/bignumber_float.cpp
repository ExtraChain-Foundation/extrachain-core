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

using boost::multiprecision::cpp_bin_float_50;

BigNumberFloat::BigNumberFloat()
    : m_data(0) {
}

BigNumberFloat::BigNumberFloat(const std::string &bigNumberFloat, int base) {
    try {
        if (bigNumberFloat.empty()) {
            this->m_data = cpp_bin_float_50(0);
        } else {
            if (base == 10) {
                this->m_data = cpp_bin_float_50(bigNumberFloat);
            } else {
                std::stringstream ss;
                ss << std::hex << bigNumberFloat;
                ss >> m_data;
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

BigNumberFloat::BigNumberFloat(const BigNumber &other) {
    this->m_data = boost::multiprecision::cpp_bin_float_50(other.data());
}

BigNumberFloat::BigNumberFloat(const boost::multiprecision::cpp_bin_float_50 &number) {
    this->m_data = number;
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(int number) {
    this->m_data = cpp_bin_float_50(number);
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(long long number) {
    this->m_data = cpp_bin_float_50(number);
    UPDATE_DEBUG()
}

BigNumberFloat::BigNumberFloat(uint64_t number) {
    this->m_data = cpp_bin_float_50(number);
    UPDATE_DEBUG()
}

BigNumberFloat BigNumberFloat::operator+(const BigNumberFloat &bigNumberFloat) {
    return BigNumberFloat(m_data + bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator+(long long number) {
    return BigNumberFloat(m_data + number);
}

BigNumberFloat BigNumberFloat::operator-(const BigNumberFloat &bigNumberFloat) {
    return BigNumberFloat(m_data - bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator-(long long number) {
    return BigNumberFloat(m_data - number);
}

BigNumberFloat BigNumberFloat::operator*(const BigNumberFloat &bigNumberFloat) const {
    return BigNumberFloat(m_data * bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator*(long long number) {
    return BigNumberFloat(m_data * number);
}

BigNumberFloat BigNumberFloat::operator/(const BigNumberFloat &bigNumberFloat) {
    return BigNumberFloat(m_data / bigNumberFloat.data());
}

BigNumberFloat BigNumberFloat::operator/(long long number) {
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
    return m_data;
}

BigNumberFloat &BigNumberFloat::operator--() {
    m_data--;
    UPDATE_DEBUG()
    return *this;
}

BigNumberFloat BigNumberFloat::operator--(int) {
    --m_data;
    UPDATE_DEBUG()
    return m_data;
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

const cpp_bin_float_50 &BigNumberFloat::data() const {
    return m_data;
}

bool BigNumberFloat::isEmpty() const // TODO
{
    return m_data == -1;
}

QByteArray BigNumberFloat::toByteArray(int base) const {
    auto res = toStdString(base);
    return res.c_str();
}

std::string BigNumberFloat::toStdString(int base) const {
    if (base == 10) {
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
    if (max.toByteArray(16).length() < n)
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
        int size = QRandomGenerator::global()->bounded(1, max.toByteArray().size());
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

QDebug operator<<(QDebug debug, const BigNumberFloat &bigNumberFloat) {
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << bigNumberFloat.toByteArray();
    return debug;
}

QDebug operator<<(QDebug debug, const cpp_bin_float_50 &bigNumberFloat) {
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << BigNumberFloat(bigNumberFloat).toByteArray();
    return debug;
}

std::ostream &operator<<(std::ostream &os, const BigNumberFloat &bigNumberFloat) {
    os << bigNumberFloat.toStdString();
    return os;
}
