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
#include "utils/exc_logs.h"
#include <exception>

#ifdef QT_DEBUG
    #define UPDATE_DEBUG() qdata = to_string()
#else
    #define UPDATE_DEBUG()
#endif

mpd_context_t *BigNumberFloat::get_context() {
    static mpd_context_t ctx;
    static bool          initialized = false;
    if (!initialized) {
        mpd_maxcontext(&ctx);
        ctx.prec  = 60;
        ctx.emax  = 30000;
        ctx.emin  = -30000 + 1;
        ctx.round = MPD_ROUND_HALF_EVEN;
        ctx.traps = 0;
        ctx.clamp = 1;
        initialized = true;
    }
    return &ctx;
}

void BigNumberFloat::init_from_string(const std::string &str) {
    if (m_data) {
        mpd_del(m_data);
    }
    m_data = mpd_new(get_context());
    mpd_set_string(m_data, str.c_str(), get_context());
}

BigNumberFloat::BigNumberFloat() {
    m_data = mpd_new(get_context());
    mpd_set_i64(m_data, 0, get_context());
}

BigNumberFloat::BigNumberFloat(const std::string &bigNumberFloat) {
    m_data = mpd_new(get_context());
    try {
        if (bigNumberFloat.empty()) {
            mpd_set_i64(m_data, 0, get_context());
        } else if (BigNumber::is_hex_string(bigNumberFloat)) {
            *this = BigNumberFloat::from_hex(bigNumberFloat);
        } else {
            mpd_set_string(m_data, bigNumberFloat.c_str(), get_context());
        }
    } catch (std::exception &) {
        eLog("Incorrect BigNumberFloat value: {}", bigNumberFloat);
        assert(false);
    }

    UPDATE_DEBUG();
}

BigNumberFloat::BigNumberFloat(const BigNumberFloat &other) {
    m_data = mpd_new(get_context());
    mpd_copy(m_data, other.m_data, get_context());
    UPDATE_DEBUG();
}

BigNumberFloat::BigNumberFloat(BigNumberFloat &&other) noexcept {
    m_data       = other.m_data;
    other.m_data = nullptr;
    UPDATE_DEBUG();
}

BigNumberFloat::BigNumberFloat(const BigNumber &other) {
    m_data = mpd_new(get_context());
    mpd_set_string(m_data, other.to_string().c_str(), get_context());
    UPDATE_DEBUG();
}

BigNumberFloat::BigNumberFloat(int number) {
    m_data = mpd_new(get_context());
    mpd_set_i32(m_data, number, get_context());
    UPDATE_DEBUG();
}

BigNumberFloat::BigNumberFloat(long long number) {
    m_data = mpd_new(get_context());
    mpd_set_i64(m_data, number, get_context());
    UPDATE_DEBUG();
}

BigNumberFloat::BigNumberFloat(std::uint64_t number) {
    m_data = mpd_new(get_context());
    mpd_set_u64(m_data, number, get_context());
    UPDATE_DEBUG();
}

BigNumberFloat::~BigNumberFloat() {
    if (m_data) {
        mpd_del(m_data);
        m_data = nullptr;
    }
}

BigNumberFloat BigNumberFloat::operator+(const BigNumberFloat &bigNumberFloat) const {
    BigNumberFloat result;
    mpd_add(result.m_data, m_data, bigNumberFloat.m_data, get_context());
    return result;
}

BigNumberFloat BigNumberFloat::operator+(long long number) const {
    return *this + BigNumberFloat(number);
}

BigNumberFloat BigNumberFloat::operator-(const BigNumberFloat &bigNumberFloat) const {
    BigNumberFloat result;
    mpd_sub(result.m_data, m_data, bigNumberFloat.m_data, get_context());
    return result;
}

BigNumberFloat BigNumberFloat::operator-(long long number) const {
    return *this - BigNumberFloat(number);
}

BigNumberFloat BigNumberFloat::operator*(const BigNumberFloat &bigNumberFloat) const {
    BigNumberFloat result;
    mpd_mul(result.m_data, m_data, bigNumberFloat.m_data, get_context());
    return result;
}

BigNumberFloat BigNumberFloat::operator*(long long number) const {
    return *this * BigNumberFloat(number);
}

BigNumberFloat BigNumberFloat::operator/(const BigNumberFloat &bigNumberFloat) const {
    if (bigNumberFloat == 0) {
        eFatal("BigNumberFloat: Division by zero");
    }

    BigNumberFloat result;
    mpd_div(result.m_data, m_data, bigNumberFloat.m_data, get_context());
    return result;
}

BigNumberFloat BigNumberFloat::operator/(long long number) const {
    return *this / BigNumberFloat(number);
}

BigNumberFloat &BigNumberFloat::operator=(const BigNumberFloat &bigNumberFloat) {
    if (this != &bigNumberFloat) {
        if (!m_data) {
            m_data = mpd_new(get_context());
        }
        mpd_copy(m_data, bigNumberFloat.m_data, get_context());
    }
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat &BigNumberFloat::operator=(long long number) {
    if (!m_data) {
        m_data = mpd_new(get_context());
    }
    mpd_set_i64(m_data, number, get_context());
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat &BigNumberFloat::operator++() {
    mpd_t *one = mpd_new(get_context());
    mpd_set_i32(one, 1, get_context());
    mpd_add(m_data, m_data, one, get_context());
    mpd_del(one);
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat BigNumberFloat::operator++(int) {
    BigNumberFloat old = *this;
    ++(*this);
    return old;
}

BigNumberFloat &BigNumberFloat::operator--() {
    mpd_t *one = mpd_new(get_context());
    mpd_set_i32(one, 1, get_context());
    mpd_sub(m_data, m_data, one, get_context());
    mpd_del(one);
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat BigNumberFloat::operator--(int) {
    BigNumberFloat old = *this;
    --(*this);
    return old;
}

BigNumberFloat &BigNumberFloat::operator+=(const BigNumberFloat &bigNumberFloat) {
    mpd_add(m_data, m_data, bigNumberFloat.m_data, get_context());
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat &BigNumberFloat::operator+=(long long number) {
    *this += BigNumberFloat(number);
    return *this;
}

BigNumberFloat &BigNumberFloat::operator-=(const BigNumberFloat &bigNumberFloat) {
    mpd_sub(m_data, m_data, bigNumberFloat.m_data, get_context());
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat &BigNumberFloat::operator-=(long long number) {
    *this -= BigNumberFloat(number);
    return *this;
}

BigNumberFloat &BigNumberFloat::operator*=(const BigNumberFloat &bigNumberFloat) {
    mpd_mul(m_data, m_data, bigNumberFloat.m_data, get_context());
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat &BigNumberFloat::operator*=(long long number) {
    *this *= BigNumberFloat(number);
    return *this;
}

BigNumberFloat &BigNumberFloat::operator/=(const BigNumberFloat &bigNumberFloat) {
    mpd_div(m_data, m_data, bigNumberFloat.m_data, get_context());
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat &BigNumberFloat::operator/=(long long number) {
    *this /= BigNumberFloat(number);
    UPDATE_DEBUG();
    return *this;
}

BigNumberFloat BigNumberFloat::operator-() const {
    BigNumberFloat result;
    mpd_minus(result.m_data, m_data, get_context());
    return result;
}

mpd_t *BigNumberFloat::data() const {
    return m_data;
}

std::string BigNumberFloat::to_string() const {
    char *sci = mpd_to_sci(m_data, 1);
    if (!sci) {
        return "0";
    }

    std::string s(sci);
    mpd_free(sci);

    // Convert scientific notation to fixed decimal
    auto posE = s.find('E');
    if (posE != std::string::npos) {
        std::string mant = s.substr(0, posE);
        int         exp  = std::stoi(s.substr(posE + 1));
        bool        neg  = false;

        if (!mant.empty() && mant[0] == '-') {
            neg = true;
            mant.erase(0, 1);
        }

        auto        posDot = mant.find('.');
        std::string intp   = mant.substr(0, posDot);
        std::string frac   = (posDot == std::string::npos) ? "" : mant.substr(posDot + 1);

        if (exp >= 0) {
            if (static_cast<int>(frac.size()) <= exp) {
                frac.append(exp - static_cast<int>(frac.size()), '0');
                mant = intp + frac;
                if (mant.empty()) mant = "0";
            } else {
                std::string left  = frac.substr(0, exp);
                std::string right = frac.substr(exp);
                mant              = intp + left + "." + right;
            }
        } else {
            int n = -exp;
            if (static_cast<int>(intp.size()) <= n) {
                std::string zeros(n - static_cast<int>(intp.size()), '0');
                mant = "0." + zeros + intp + frac;
            } else {
                std::string left  = intp.substr(0, static_cast<int>(intp.size()) - n);
                std::string right = intp.substr(static_cast<int>(intp.size()) - n);
                mant              = left + "." + right + frac;
            }
        }

        if (neg) mant.insert(mant.begin(), '-');
        s = mant;
    }

    // Normalize: remove leading zeros in integer part, trailing zeros in fractional part
    {
        bool        neg = false;
        std::string t   = s;
        if (!t.empty() && t[0] == '-') {
            neg = true;
            t.erase(0, 1);
        }

        auto        posDot = t.find('.');
        std::string I      = posDot == std::string::npos ? t : t.substr(0, posDot);
        std::string F      = posDot == std::string::npos ? "" : t.substr(posDot + 1);

        // Remove leading zeros in integer part
        size_t nz = I.find_first_not_of('0');
        if (nz == std::string::npos)
            I = "0";
        else if (nz > 0)
            I.erase(0, nz);

        // Remove trailing zeros in fractional part
        if (!F.empty()) {
            size_t nz2 = F.find_last_not_of('0');
            if (nz2 == std::string::npos)
                F.clear();
            else
                F.erase(nz2 + 1);
        }

        if (F.empty())
            s = (neg ? "-" : "") + I;
        else
            s = (neg ? "-" : "") + I + "." + F;
    }

    return s;
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
    BigNumberFloat result;
    mpd_t         *exp = mpd_new(get_context());
    mpd_set_u64(exp, number, get_context());
    mpd_pow(result.m_data, m_data, exp, get_context());
    mpd_del(exp);
    return result;
}

BigNumberFloat BigNumberFloat::abs() const {
    BigNumberFloat result;
    mpd_abs(result.m_data, m_data, get_context());
    return result;
}

std::expected<BigNumberFloat, BigNumberError> BigNumberFloat::create(const std::string &bigNumberFloat) {
    if (bigNumberFloat == "inf" || bigNumberFloat == "-inf") {
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
    mpd_t *other_mpd = mpd_new(get_context());
    mpd_set_i32(other_mpd, other, get_context());
    int cmp = mpd_cmp(m_data, other_mpd, get_context());
    mpd_del(other_mpd);

    if (cmp < 0) return std::strong_ordering::less;
    if (cmp > 0) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

bool BigNumberFloat::operator==(const int &other) const {
    mpd_t *other_mpd = mpd_new(get_context());
    mpd_set_i32(other_mpd, other, get_context());
    int cmp = mpd_cmp(m_data, other_mpd, get_context());
    mpd_del(other_mpd);
    return cmp == 0;
}

bool BigNumberFloat::operator!=(const int &other) const {
    return !(*this == other);
}

bool BigNumberFloat::operator==(const BigNumberFloat &other) const {
    return mpd_cmp(m_data, other.m_data, get_context()) == 0;
}

std::strong_ordering BigNumberFloat::operator<=>(const BigNumberFloat &other) const {
    int cmp = mpd_cmp(m_data, other.m_data, get_context());
    if (cmp < 0) return std::strong_ordering::less;
    if (cmp > 0) return std::strong_ordering::greater;
    return std::strong_ordering::equal;
}

bool BigNumberFloat::operator!=(const BigNumberFloat &other) const {
    return !(*this == other);
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
