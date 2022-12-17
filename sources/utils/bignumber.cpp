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

#include "utils/bignumber.h"
#include <exception>

using boost::multiprecision::cpp_int;

BigNumber::BigNumber()
    : m_data(0) {
}

BigNumber::BigNumber(const std::string &bigNumber, int base) {
    try {
        if (bigNumber.empty()) {
            this->m_data = cpp_int(0);
        } else {
            if (base == 10) {
                this->m_data = cpp_int(bigNumber);
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

BigNumber BigNumber::operator+(const BigNumber &bigNumber) {
    return BigNumber(m_data + bigNumber.data());
}

BigNumber BigNumber::operator+(long long number) {
    return BigNumber(m_data + number);
}

BigNumber BigNumber::operator-(const BigNumber &bigNumber) {
    return BigNumber(m_data - bigNumber.data());
}

BigNumber BigNumber::operator-(long long number) {
    return BigNumber(m_data - number);
}

BigNumber BigNumber::operator*(const BigNumber &bigNumber) const {
    return BigNumber(m_data * bigNumber.data());
}

BigNumber BigNumber::operator*(long long number) {
    return BigNumber(m_data * number);
}

BigNumber BigNumber::operator/(const BigNumber &bigNumber) {
    return BigNumber(m_data / bigNumber.data());
}

BigNumber BigNumber::operator/(long long number) {
    return BigNumber(m_data / number);
}

BigNumber BigNumber::operator%(const BigNumber &bigNumber) {
    return BigNumber(m_data % bigNumber.data());
}

BigNumber BigNumber::operator%(const BigNumber &bigNumber) const {
    return BigNumber(m_data % bigNumber.data());
}

BigNumber BigNumber::operator%(long long number) {
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
    return m_data;
}

BigNumber &BigNumber::operator--() {
    m_data--;
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator--(int) {
    --m_data;
    UPDATE_DEBUG()
    return m_data;
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

bool BigNumber::isEmpty() const // TODO
{
    return m_data == -1;
}

QByteArray BigNumber::toByteArray(int base) const {
    auto res = toStdString(base);
    return res.c_str();
}

std::string BigNumber::toStdString(int base) const {
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

std::string BigNumber::toZeroStdString(int size) const {
    auto number = this->toStdString();
    if (size <= number.length())
        return number;
    number.insert(0, size - number.size(), '0');
    return number;
}

BigNumber BigNumber::pow(unsigned long number) {
    auto res = boost::multiprecision::pow(m_data, number);
    return BigNumber(res);
}

BigNumber BigNumber::abs() const {
    auto res = boost::multiprecision::abs(m_data);
    return BigNumber(res);
}

BigNumber BigNumber::random(int n, bool zeroAllowed) {
    QByteArray str;
    str.resize(n);
    str[0] = '0';

    while (str[0] == '0')
        str[0] = BigNumberUtils::Chars[QRandomGenerator::global()->bounded(16)];

    for (int i = 1; i != n; ++i)
        str[i] = BigNumberUtils::Chars[QRandomGenerator::global()->bounded(16)];

    BigNumber res(str.toStdString());
    if (!zeroAllowed && res == 0)
        return random(n, zeroAllowed);
    return res;
}

BigNumber BigNumber::random(int n, const BigNumber &max, bool zeroAllowed) {
    if (max.toByteArray(16).length() < n)
        return BigNumber(0);

    BigNumber result;

    do {
        result = random(n, zeroAllowed);
    } while (result >= max);
    return result;
}

BigNumber BigNumber::random(BigNumber max, bool zeroAllowed) {
    QByteArray maxdata = max.toByteArray();
    QByteArray b;
    b.clear();
    b.fill('f', maxdata.size());
    BigNumber t(b.toStdString());

    while (t >= max) {
        int size = QRandomGenerator::global()->bounded(1, max.toByteArray().size());
        QByteArray res;
        res.clear();
        for (int i = 0; i < size; i++) {
            res.append(BigNumberUtils::Chars[QRandomGenerator::global()->bounded(0, 15)]);
        }
        t = BigNumber(res.toStdString());
    }
    if (!zeroAllowed && t == 0)
        return random(max, zeroAllowed);
    return t;
}

QDebug operator<<(QDebug debug, const BigNumber &bigNumber) {
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << bigNumber.toByteArray();
    return debug;
}

QDebug operator<<(QDebug debug, const cpp_int &bigNumber) {
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << BigNumber(bigNumber).toByteArray();
    return debug;
}

std::ostream &operator<<(std::ostream &os, const BigNumber &bigNumber) {
    os << bigNumber.toStdString();
    return os;
}
