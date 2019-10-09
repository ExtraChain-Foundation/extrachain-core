#include "utils/bignumber.h"

#ifdef QT_DEBUG
#include <QRegularExpression>
#endif

BigNumber::BigNumber()
{
    this->m_data = nullptr;
    this->m_base = 16;
}

BigNumber::BigNumber(const QByteArray &bigNumber, int base)
{
    this->m_data = new mpz_class(bigNumber.toStdString(), base);
    this->m_base = base;
}

BigNumber::BigNumber(const BigNumber &other)
{
    this->m_data = new mpz_class(other.data());
    this->m_base = other.base();
    //*(this->m_data) = other.data();
}

BigNumber::BigNumber(mpz_class data)
{
    this->m_base = 16;
    this->m_data = new mpz_class(data);
}

BigNumber::BigNumber(int number)
{
    this->m_base = 10;
    this->m_data = new mpz_class(number);
}

BigNumber::BigNumber(long long number)
{
    this->m_base = 10;
    this->m_data = new mpz_class(std::to_string(number));
}

BigNumber::~BigNumber()
{
    delete m_data;
}

int BigNumber::base() const
{
    return m_base;
}

mpz_class BigNumber::data() const
{
    if (m_data == nullptr)
        return NULL;
    else
        return *m_data;
}

BigNumber BigNumber::operator&(const BigNumber &value)
{ /////????

    BigNumber da(*(this->m_data) & value.data());
    return da;
}

BigNumber BigNumber::operator>>(const uint &value)
{
    BigNumber ret(*(this->m_data) >> value);
    return ret;
}

BigNumber BigNumber::operator>>=(const uint &value)
{
    BigNumber ret(*(this->m_data) >> value);
    *(this->m_data) = ret.data();
    return *this;
}

BigNumber BigNumber::operator+(const BigNumber &other)
{
    BigNumber ret(*(this->m_data) + other.data());
    return ret;
}

BigNumber BigNumber::operator+(long long number)
{
    BigNumber ret(*(this->m_data) + BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator-(const BigNumber &bigNumber)
{
    BigNumber ret(*(this->m_data) - bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator-(long long number)
{
    BigNumber ret(*(this->m_data) - BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator*(const BigNumber &bigNumber)
{
    BigNumber ret(*(this->m_data) * bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator*(long long number)
{
    BigNumber ret(*(this->m_data) * BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator/(const BigNumber &bigNumber)
{
    if (*(this->m_data) >= bigNumber.data())
        std::cout << "true" << std::endl;
    else
        std::cout << "false" << std::endl;
    BigNumber ret(*(this->m_data) / bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator/(long long number)
{
    BigNumber ret(*(this->m_data) / BigNumber(number).data());
    return ret;
}

BigNumber BigNumber::operator%(const BigNumber &bigNumber)
{
    BigNumber ret(*(this->m_data) % bigNumber.data());
    return ret;
}

BigNumber BigNumber::operator%(long long number)
{
    BigNumber ret(*(this->m_data) % BigNumber(number).data());
    return ret;
}

BigNumber &BigNumber::operator=(const BigNumber &bigNumber)
{
    if (this->m_data == nullptr)
        this->m_data = new mpz_class();
    this->m_base = bigNumber.base();
    *(this->m_data) = bigNumber.data();
    return *this;
}

BigNumber &BigNumber::operator=(long long number)
{
    if (this->m_data == nullptr)
        this->m_data = new mpz_class();
    *(this->m_data) = mpz_class(std::to_string(number));
    this->m_base = 10;
    return *this;
}

BigNumber &BigNumber::operator++()
{
    *this = *this + 1;
    return *this;
}

BigNumber BigNumber::operator++(int)
{
    ++*this->m_data;
    return *this->m_data;
}

BigNumber &BigNumber::operator--()
{
    (*this->m_data)--;
    return *this;
}

BigNumber BigNumber::operator--(int)
{
    --*this->m_data;
    return *this->m_data;
}

BigNumber &BigNumber::operator+=(const BigNumber &bigNumber)
{
    *this = *this + bigNumber;
    return *this;
}

BigNumber &BigNumber::operator+=(long long number)
{
    *this = *this + number;
    return *this;
}

BigNumber &BigNumber::operator-=(const BigNumber &bigNumber)
{
    *this = *this - bigNumber;
    return *this;
}

BigNumber &BigNumber::operator-=(long long number)
{
    *this = *this - number;
    return *this;
}

BigNumber &BigNumber::operator*=(const BigNumber &bigNumber)
{
    *this = *this * bigNumber;
    return *this;
}

BigNumber &BigNumber::operator*=(long long number)
{
    *this = *this * number;
    return *this;
}

BigNumber &BigNumber::operator/=(const BigNumber &bigNumber)
{
    *this = *this / bigNumber;
    return *this;
}

BigNumber &BigNumber::operator/=(long long number)
{
    *this = *this / number;
    return *this;
}

BigNumber &BigNumber::operator%=(const BigNumber &bigNumber)
{
    *this = *this % bigNumber;
    return *this;
}

BigNumber &BigNumber::operator%=(long long number)
{
    *this = *this % number;
    return *this;
}

BigNumber BigNumber::operator-()
{
    BigNumber res(0 - (*this->m_data));
    return res;
}

bool BigNumber::isPrime() const
{
    return true;
}

bool BigNumber::isEmpty() const
{
    return this->m_data == nullptr;
}

QByteArray BigNumber::toByteArray(int base) const
{
    //    std::string s = m_data->get_str(16);
    //    std::cout << "ToByteArray: " << s << " base: " << base << std::endl;
    return QByteArray::fromStdString(m_data->get_str(base));
}

QByteArray BigNumber::serialize() const
{
    return toByteArray();
}

BigNumber BigNumber::pow(unsigned long long number) // naive
{
    BigNumber result = 1;
    for (unsigned long long i = 0; i != number; ++i)
        result = result * *this;
    return result;
}

BigNumber BigNumber::sqrt(const BigNumber &value)
{
    BigNumber a("1");
    BigNumber b = value;
    BigNumber c = (a + b) / 2;

    while (b - c > BigNumber("1"))
    {
        if (c * c == value)
            return c;
        else if (c * c > value)
            b = c;
        else
            a = c;

        c = (a + b) / 2;
    }

    while ((c + 1) * (c + 1) < value)
    {
        c++;
    }
    return c;
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
    BigNumber t(0);
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
