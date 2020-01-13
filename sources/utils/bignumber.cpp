#include "utils/bignumber.h"
#include <exception>

BigNumber::BigNumber()
    : m_data(0)
{
}

BigNumber::BigNumber(const QByteArray &bigNumber, int base)
{
    try
    {
        if (bigNumber.isEmpty())
            this->m_data = mpz_class(0);
        else
            this->m_data = mpz_class(bigNumber.toStdString(), base);
    } catch (std::exception &)
    {
        qDebug() << "Incorrect BigNumber value:" << bigNumber << "with base" << base;
        assert(false);
    }

    UPDATE_DEBUG()
}

BigNumber::BigNumber(const BigNumber &other)
{
    this->m_data = other.data();
    UPDATE_DEBUG()
}

BigNumber::BigNumber(mpz_class number)
{
    this->m_data = mpz_class(number);
    UPDATE_DEBUG()
}

BigNumber::BigNumber(int number)
{
    this->m_data = mpz_class(number);
    UPDATE_DEBUG()
}

BigNumber::BigNumber(long long number)
{
    this->m_data = mpz_class(std::to_string(number));
    UPDATE_DEBUG()
}

BigNumber BigNumber::operator&(const BigNumber &value)
{
    BigNumber da(m_data & value.data());
    return da;
}

BigNumber BigNumber::operator>>(const uint &value)
{
    BigNumber ret(m_data >> value);
    return ret;
}

BigNumber BigNumber::operator>>=(const uint &value)
{
    BigNumber ret(m_data >> value);
    m_data = ret.data();
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator+(const BigNumber &other)
{
    BigNumber ret(m_data + other.data());
    return ret;
}

BigNumber BigNumber::operator+(long long number)
{
    BigNumber ret(m_data + BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator-(const BigNumber &bigNumber)
{
    BigNumber ret(m_data - bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator-(long long number)
{
    BigNumber ret(m_data - BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator*(const BigNumber &bigNumber) const
{
    BigNumber ret(m_data * bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator*(long long number)
{
    BigNumber ret(m_data * BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator/(const BigNumber &bigNumber)
{
    BigNumber ret(m_data / bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator/(long long number)
{
    BigNumber ret(m_data / BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator%(const BigNumber &bigNumber)
{
    BigNumber ret(m_data % bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator%(long long number)
{
    BigNumber ret(m_data % BigNumber(number).data());
    return ret;
}

BigNumber &BigNumber::operator=(const BigNumber &bigNumber)
{
    m_data = bigNumber.data();
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator=(long long number)
{
    m_data = mpz_class(std::to_string(number));
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator++()
{
    *this = *this + 1;
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator++(int)
{
    ++m_data;
    UPDATE_DEBUG()
    return m_data;
}

BigNumber &BigNumber::operator--()
{
    m_data--;
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator--(int)
{
    --m_data;
    UPDATE_DEBUG()
    return m_data;
}

BigNumber &BigNumber::operator+=(const BigNumber &bigNumber)
{
    *this = *this + bigNumber;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator+=(long long number)
{
    *this = *this + number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator-=(const BigNumber &bigNumber)
{
    *this = *this - bigNumber;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator-=(long long number)
{
    *this = *this - number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator*=(const BigNumber &bigNumber)
{
    *this = *this * bigNumber;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator*=(long long number)
{
    *this = *this * number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator/=(const BigNumber &bigNumber)
{
    *this = *this / bigNumber;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator/=(long long number)
{
    *this = *this / number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator%=(const BigNumber &bigNumber)
{
    *this = *this % bigNumber;
    UPDATE_DEBUG()
    return *this;
}

BigNumber &BigNumber::operator%=(long long number)
{
    *this = *this % number;
    UPDATE_DEBUG()
    return *this;
}

BigNumber BigNumber::operator-() const
{
    return BigNumber(-m_data);
}

mpz_class BigNumber::data() const
{

    return m_data;
}

int BigNumber::isProbPrime() const
{
    return mpz_probab_prime_p(m_data.get_mpz_t(), 10);
}

bool BigNumber::isEmpty() const // TODO
{
    return m_data == -1;
}

QByteArray BigNumber::toByteArray(int base) const
{
    char *ch = mpz_get_str(nullptr, base, m_data.get_mpz_t());
    QByteArray number(ch);
    void (*freefunc)(void *, size_t);
    mp_get_memory_functions(NULL, NULL, &freefunc);
    freefunc(ch, strlen(ch) + 1);
    return number;
}

std::string BigNumber::toStdString(int base) const
{
    return m_data.get_str(base);
}

QByteArray BigNumber::toActorId() const
{
    QByteArray actorId = this->toByteArray();
    while (actorId.length() < 20)
    {
        actorId.push_front('0');
    }
    return actorId;
}

BigNumber BigNumber::pow(unsigned long number)
{
    mpz_class res;
    mpz_pow_ui(res.get_mpz_t(), data().get_mpz_t(), number);
    return res;
}

BigNumber BigNumber::sqrt(unsigned long number) const
{
    mpz_class res;
    mpz_root(res.get_mpz_t(), m_data.get_mpz_t(), number);
    return res;
}

BigNumber BigNumber::abs() const
{
    mpz_class res;
    mpz_abs(res.get_mpz_t(), m_data.get_mpz_t());
    return res;
}

bool BigNumber::getInfinity() const
{
    return infinity;
}

void BigNumber::setInfinity(bool value)
{
    infinity = value;

    if (value)
        m_data = 0;
}

bool BigNumber::isValid(const QByteArray &bigNumber, int base)
{
    if (bigNumber.isEmpty())
        return false;
    try
    {
        mpz_class(bigNumber.toStdString(), base);
        return true;
    } catch (std::exception &)
    {
        return false;
    }
}

BigNumber BigNumber::factorial(unsigned long number)
{
    mpz_class res;
    mpz_fac_ui(res.get_mpz_t(), number);
    return res;
}

BigNumber BigNumber::random(int n, bool zeroAllowed)
{
    QByteArray str;
    str.reserve(n);
    str[0] = '0';

    while (str[0] == '0')
        str[0] = BigNumberUtils::Chars[QRandomGenerator::global()->bounded(16)];

    for (int i = 1; i != n; ++i)
        str[i] = BigNumberUtils::Chars[QRandomGenerator::global()->bounded(16)];
    // std::cout << str.toStdString() << std::endl;
    BigNumber res(str);
    if (!zeroAllowed && res == 0)
        return random(n, zeroAllowed);
    return res;
}

BigNumber BigNumber::random(int n, const BigNumber &max, bool zeroAllowed)
{
    if (max.toByteArray(16).length() < n)
        return BigNumber(0);

    BigNumber result;

    do
    {
        result = random(n, zeroAllowed);
    } while (result >= max);
    return result;
}

BigNumber BigNumber::random(BigNumber max, bool zeroAllowed)
{
    QByteArray maxdata = max.toByteArray();
    QByteArray b;
    b.clear();
    b.fill('f', maxdata.size());
    BigNumber t(b);

    while (t >= max)
    {
        int size = QRandomGenerator::global()->bounded(1, max.toByteArray().size());
        QByteArray res;
        res.clear();
        for (int i = 0; i < size; i++)
        {
            res.append(BigNumberUtils::Chars[QRandomGenerator::global()->bounded(0, 15)]);
        }
        t = BigNumber(res);
    }
    if (!zeroAllowed && t == 0)
        return random(max, zeroAllowed);
    return t;
}

QDebug operator<<(QDebug debug, const BigNumber &bigNumber)
{
    QDebugStateSaver saver(debug);

    if (bigNumber >= 0)
        debug.nospace().noquote() << "0x" << bigNumber.toByteArray(16);
    else
        debug.nospace().noquote() << "-0x" << bigNumber.abs().toByteArray(16);

    return debug;
}

QDebug operator<<(QDebug debug, const mpz_class &bigNumber)
{
    QDebugStateSaver saver(debug);

    if (bigNumber >= 0)
    {
        debug.nospace().noquote() << "0x" << bigNumber.get_str(16).c_str();
    }
    else
    {
        mpz_class num = -bigNumber;
        debug.nospace().noquote() << "-0x" << num.get_str(16).c_str();
    }

    return debug;
}
