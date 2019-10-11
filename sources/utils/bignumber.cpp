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
        this->m_data = mpz_class(bigNumber.toStdString(), base);
    } catch (std::exception a)
    {
        std::cout << "! BigNumber incorrect value: \"" << bigNumber.toStdString() << "\"" << std::endl;
        std::exit(-1500);
    }

    UPDATE_DEBUG()
}

BigNumber::BigNumber(const BigNumber &other)
{
    this->m_data = mpz_class(other.data());
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

BigNumber BigNumber::operator*(const BigNumber &bigNumber)
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
    //    if (m_data >= bigNumber.data())
    //        std::cout << "true" << std::endl;
    //    else
    //        std::cout << "false" << std::endl;
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
    m_data = mpz_class(bigNumber.data());
    UPDATE_DEBUG()
    //    m_data = ;
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

BigNumber BigNumber::operator-()
{
    BigNumber res(0 - m_data);
    return res;
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
    return m_data == 0;
}

QByteArray BigNumber::toByteArray(int base) const
{
    return QByteArray::fromStdString(m_data.get_str(base));
}

QByteArray BigNumber::serialize() const
{
    return toByteArray();
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

BigNumber BigNumber::factorial(unsigned long number)
{
    mpz_class res;
    mpz_fac_ui(res.get_mpz_t(), number);
    return res;
}

BigNumber BigNumber::random(int n)
{
    const static std::vector<char> chars = { 'a', 'b', 'c', 'd', 'e', 'f', '0', '1',
                                             '2', '3', '4', '5', '6', '7', '8', '9' };
    QByteArray str;
    str.reserve(n);
    str[0] = '0';

    while (str[0] == '0')
        str[0] = chars[std::size_t(QRandomGenerator::global()->bounded(16))];

    for (int i = 1; i != n; ++i)
        str[i] = chars[std::size_t(QRandomGenerator::global()->bounded(16))];
    std::cout << str.toStdString() << std::endl;
    return BigNumber(str);
}

BigNumber BigNumber::random(int n, const BigNumber &max)
{
    if (max.toByteArray(16).length() < n)
        return BigNumber(0);

    BigNumber result;

    do
    {
        result = random(n);
    } while (result >= max);
    std::cout << "random n max: " << result.toByteArray().toStdString() << std::endl;
    return result;
}

BigNumber BigNumber::random(const BigNumber &max)
{
    BigNumber bt = max;
    BigNumber t(QByteArray().fill('f', bt.toByteArray().size()));
    while (t >= max)
    {
        int size = QRandomGenerator::global()->bounded(1, max.toByteArray().size());
        QByteArray res;
        res.clear();
        for (int i = 0; i < size; i++)
        {
            res.append(QByteArray::number(QRandomGenerator::global()->bounded(0, 9)));
        }
        t = BigNumber(res);
    }

    return t;
}

QDebug operator<<(QDebug debug, const BigNumber &bigNumber)
{
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << "\"0x" << bigNumber.toByteArray(16) << "\"";
    return debug;
}

QDebug operator<<(QDebug debug, const mpz_class &bigNumber)
{
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << "\"0x" << bigNumber.get_str(16).c_str() << "\"";
    return debug;
}
