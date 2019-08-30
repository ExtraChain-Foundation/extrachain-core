#include "utils/bignumber.h"

BigNumber::BigNumber()
{
}

BigNumber::BigNumber(const QByteArray &bigNumber)
{
    this->setHexValue(bigNumber);
}

BigNumber::BigNumber(const BigNumber &other)
{
    this->setHexValue(other.getHexValue());
    this->setPositive(other.isPositive());
}

BigNumber::BigNumber(int number)
{
    if (number < 0)
    {
        number *= -1;
        this->positive = false;
    }

    hexValue = toHex(QString::number(number));
}

BigNumber::BigNumber(long long number)
{
    if (number < 0)
    {
        number *= -1;
        this->positive = false;
    }

    this->hexValue = toHex(QString::number(number));
}

BigNumber::~BigNumber()
{
}

QString BigNumber::toHex(const QString &dec) const
{
    qlonglong decVal = dec.toLongLong(nullptr, DEC_BASE);
    QString hexStr = QString::number(decVal, HEX_BASE);
    return hexStr;
}

QString BigNumber::toDec(const QString &hex) const
{
    qlonglong decVal = hex.toLongLong(nullptr, HEX_BASE);
    QString dec = QString::number(decVal, DEC_BASE);
    return dec;
}

QString BigNumber::fillZeros(const QString &number) const
{
    QString result = number;
    while (number.size() <= LONG_LONG_LENGTH)
    {
        result = "0" + result;
    }

    return result;
}

QString BigNumber::cutZeros(const QString &number) const
{
    if (number.startsWith("0") && number.size() > 1)
    {
        int zeros = 0;
        for (int i = 0; i < number.size(); i++)
        {
            if (number.at(i) == QChar('0'))
            {
                zeros++;
            }
            else
                break;
        }

        string val = number.toStdString().substr(std::size_t(zeros), std::size_t(number.size()));
        return QString(val.c_str());
    }

    return number;
}

int BigNumber::compare(const QString &one, const QString &two)
{

    if (one.compare(two) == 0)
        return 0;

    if (one.size() > two.size())
        return 1;

    if (one.size() < two.size())
        return -1;

    // one.size = two.size
    return one.compare(two);
}

BigNumber BigNumber::operator&(const BigNumber &value)
{
    int mineSize = this->toBinary().size();
    int valueSize = value.toBinary().size();

    int size = 0;
    if (mineSize > valueSize)
        size = valueSize;
    else
        size = mineSize;

    QByteArray result;
    mineSize -= size;
    valueSize -= size;
    // QString somestr = QString::number(this->toString().toInt(),16);

    for (int i = 0; i < size; i++)
    {

        result.append(
            BigNumber::binaryCompareAnd(this->toBinary()[mineSize + i], value.toBinary()[valueSize + i]));
    }
    //[mineSize-i-1]
    //[valueSize-i-1]
    return result;
}

std::pair<BigNumber, BigNumber> BigNumber::naiveDivide(BigNumber &value, const BigNumber &divider)
{
    BigNumber result("0");
    while (value >= divider)
    {
        value = value - divider;
        ++result;
    }

    return std::make_pair(result, value);
}

std::pair<BigNumber, BigNumber> BigNumber::divide(BigNumber val, BigNumber divider)
{
    QString value = val.hexValue;

    if (divider.hexValue.isEmpty() || divider.hexValue == "0")
        return std::make_pair(val, 0);

    if (compare(divider.hexValue, value) > 0)
        return std::make_pair(0, val);

    if (val == divider)
        return std::make_pair(1, 0);

    bool resultPositive = true;
    bool modPositive = true;

    if (!val.isPositive() && !divider.isPositive())
    {
        resultPositive = true;
        modPositive = false;
    }
    else if (!val.isPositive() || !divider.isPositive())
    {
        resultPositive = false;
        modPositive = false;
    }

    if (val.isPositive() && !divider.isPositive())
        modPositive = true;

    val.positive = true;
    divider.positive = true;

    QString temp, resultStr;
    int len = 0, length = value.length();
    BigNumber mod;
    int last = 0;

    do
    {
        if (temp[0] == '0')
            temp = temp.remove(0, 1);

        QString num = value.mid(len, 1);
        BigNumber tempBig(temp.toLocal8Bit());
        BigNumber tempDivider(divider);
        tempDivider.positive = true;

        if (tempBig < tempDivider)
        {
            temp += num;
            if (!resultStr.isEmpty())
                resultStr += "0";
        }
        else
        {
            last = len;
            auto tempDivResult = naiveDivide(tempBig, divider);
            auto div = tempDivResult.first;
            mod = tempDivResult.second;

            resultStr += div.hexValue;

            if ((num == "0" && value.mid(len + 1, 1) == "0")
                && (mod.hexValue == "0" || mod.hexValue.isEmpty()))
            {
                bool ok = true;
                int i;

                for (i = len; i != length; ++i)
                {
                    if (value.mid(i, 1) != "0")
                    {
                        ok = false;
                        break;
                    }
                }

                if (ok)
                {
                    resultStr += QString(i - len, '0');
                    temp = "";
                    break;
                }
            }

            temp = mod.hexValue + num;
            mod.hexValue += num;
            if (temp[0] == '0')
                temp = temp.remove(0, 1);
        }
    } while (len++ != length);

    BigNumber result(resultStr.toLocal8Bit());
    result.setPositive(resultPositive);

    mod.setHexValue(mod.hexValue);
    if (last < length - 1 && BigNumber(temp.toLocal8Bit()) > mod)
        mod.setHexValue(temp);
    mod.setPositive(modPositive);

    return std::make_pair(result, mod);
}

BigNumber BigNumber::operator+(const BigNumber &other)
{
    // some temp fixes for negative numbers
    if (!this->isPositive() && other.isPositive())
    {
        BigNumber one = *this;
        BigNumber two = other;
        one.positive = true;

        BigNumber result = one - two;
        result.positive = result.hexValue != "0" ? one < two : true;

        return result;
    }

    if (this->isPositive() && !other.isPositive())
    {
        BigNumber one = *this;
        BigNumber two = other;
        two.positive = true;

        BigNumber result = two - one;
        result.positive = result.hexValue != "0" ? one > two.abs() : true;

        return result;
    }

    QString hex1 = this->hexValue;
    QString hex2 = other.getHexValue();

    if (hex1.length() < hex2.length())
        hex1.swap(hex2);

    int length1 = hex1.length();
    int length2 = hex2.length();
    int flag = 0; // carry
    int get1, get2, sum;

    while (length1 > 0)
    {
        // get first number
        get1 = hex1.mid(length1 - 1, 1).toInt(nullptr, HEX_BASE);

        // get second number
        if (length2 > 0)
            get2 = hex2.mid(length2 - 1, 1).toInt(nullptr, HEX_BASE);
        else
            get2 = 0;

        // get the sum
        sum = get1 + get2 + flag;

        if (sum >= HEX_BASE)
        {
            int left = sum % HEX_BASE;
            hex1[length1 - 1] = QString::number(left, HEX_BASE).at(0);
            flag = 1;
        }
        else
        {
            hex1[length1 - 1] = QString::number(sum, HEX_BASE).at(0);
            flag = 0;
        }

        length1--;
        length2--;
    }

    BigNumber result = (flag == 1 ? BigNumber(("1" + hex1).toLocal8Bit()) : BigNumber(hex1.toLocal8Bit()));

    if (!this->isPositive() && !other.isPositive())
        result.positive = !result.positive;

    if (result.hexValue == "0" || result.hexValue.isEmpty())
        result.setHexValue("0");

    return result;
}

BigNumber BigNumber::operator+(long long number)
{
    QByteArray hexVal = toHex(QString::number(number)).toLocal8Bit();
    return *this + BigNumber(hexVal);
}

BigNumber BigNumber::operator-(const BigNumber &bigNumber)
{
    if (!this->isPositive() && bigNumber.isPositive())
    {
        BigNumber one = *this;
        one.positive = true;

        BigNumber res = one + bigNumber;
        res.setPositive(false);

        return res;
    }

    if (this->isPositive() && !bigNumber.isPositive())
    {
        BigNumber two = bigNumber;
        two.positive = true;

        BigNumber res(*this + two);

        return res;
    }

    QString hex1 = this->hexValue;
    QString hex2 = bigNumber.getHexValue();
    BigNumber result;
    QString res_s;
    if (compare(hex2, hex1) > 0)
    {
        result.positive = false;
        hex1.swap(hex2);
    }

    if (!this->isPositive() && !bigNumber.isPositive())
        result.positive = !result.positive;

    int length1 = hex1.length();
    int length2 = hex2.length();
    int flag = 0; // carry
    int get1, get2;
    int div;

    while (length1 > 0)
    {
        // get first number
        get1 = hex1.mid(length1 - 1, 1).toInt(nullptr, HEX_BASE);

        // get second number
        if (length2 > 0)
            get2 = hex2.mid(length2 - 1, 1).toInt(nullptr, HEX_BASE);
        else
            get2 = 0;

        div = get1 - get2 - flag;

        if (div < 0)
        {
            int convertVal = HEX_BASE + get1 - flag;
            convertVal = convertVal - get2;
            res_s.insert(0, QString::number(convertVal, HEX_BASE).at(0));
            // hex1[length1-1] = QString::number(convertVal, HEX_BASE).at(0);
            flag = 1;
        }
        else
        {
            res_s.insert(0, QString::number(div, HEX_BASE).at(0));
            flag = 0;
        }

        length1--;
        length2--;
    }

    result.hexValue = cutZeros(res_s);

    if (result.hexValue == "0" || result.hexValue.isEmpty())
        result.setHexValue("0");

    return result;
}

BigNumber BigNumber::operator-(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this - BigNumber(hexVal.toLocal8Bit());
}

BigNumber BigNumber::operator*(const BigNumber &bigNumber)
{
    if (bigNumber.getHexValue() == "0" || this->hexValue == "0")
        return BigNumber();

    bool resultPositive = true;
    if (!this->isPositive() && !bigNumber.isPositive())
        resultPositive = true;
    else if (!this->isPositive() || !bigNumber.isPositive())
        resultPositive = false;

    BigNumber two = bigNumber;
    two.positive = true;

    QString multi = two.getHexValue();
    int length = multi.size();
    int digit = 0;

    BigNumber value = BigNumber(*this);
    value.positive = true;
    BigNumber result;

    while (length > 0)
    {
        int repeat = multi.mid(length - 1, 1).toInt(nullptr, HEX_BASE);
        BigNumber columnResult;

        for (int i = 1; i <= repeat; repeat--)
            columnResult = columnResult + value;

        for (int i = 0; i < digit; i++)
            columnResult.hexValue = columnResult.getHexValue().append("0");

        result = result + columnResult;
        digit++;
        length--;
    }

    result.setPositive(resultPositive);
    return result;
}

BigNumber BigNumber::operator*(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this * BigNumber(hexVal.toLocal8Bit());
}

BigNumber BigNumber::operator/(const BigNumber &divider)
{
    return BigNumber::divide(*this, divider).first;
}

BigNumber BigNumber::operator/(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this / BigNumber(hexVal.toLocal8Bit());
}

BigNumber BigNumber::operator%(const BigNumber &divider)
{
    return BigNumber::divide(*this, divider).second;
}

BigNumber BigNumber::operator%(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this % BigNumber(hexVal.toLocal8Bit());
}

BigNumber &BigNumber::operator=(const BigNumber &bigNumber)
{
    this->hexValue = bigNumber.getHexValue();
    this->setPositive(bigNumber.isPositive());
    return *this;
}

BigNumber &BigNumber::operator=(long long number)
{
    if (number < 0)
    {
        number *= -1;
        this->positive = false;
    }

    this->hexValue = toHex(QString::number(number));
    return *this;
}

BigNumber &BigNumber::operator++()
{
    *this = *this + 1;
    return *this;
}

BigNumber BigNumber::operator++(int)
{
    BigNumber val = *this;
    ++(*this);
    return val;
}

BigNumber &BigNumber::operator--()
{
    *this = *this - 1;
    return *this;
}

BigNumber BigNumber::operator--(int)
{
    BigNumber val = *this;
    --(*this);
    return val;
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

BigNumber &BigNumber::operator-()
{
    this->setPositive(!positive);
    return *this;
}

bool BigNumber::isPrime() const
{
    BigNumber currentThis = *this;
    BigNumber n = sqrt(*this);

    if (*this == 2)
        return true;
    if (currentThis % 2 == 0 || *this == 1)
        return false;

    for (BigNumber i = 3; i <= n; i = i + 2)
    {
        if (currentThis % i == 0)
            return false;
    }

    return true;
}

bool BigNumber::isEmpty() const
{
    return *this == -1;
}

bool BigNumber::isPositive() const
{
    return this->positive;
}

QString BigNumber::getHexValue() const
{
    return this->hexValue;
}

QByteArray BigNumber::toBinary() const
{
    QMap<char, QByteArray> map;
    map.insert('0', "0000");
    map.insert('1', "0001");
    map.insert('2', "0010");
    map.insert('3', "0011");
    map.insert('4', "0100");
    map.insert('5', "0101");
    map.insert('6', "0110");
    map.insert('7', "0111");
    map.insert('8', "1000");
    map.insert('9', "1001");
    map.insert('a', "1010");
    map.insert('b', "1011");
    map.insert('c', "1100");
    map.insert('d', "1101");
    map.insert('e', "1110");
    map.insert('f', "1111");
    QByteArray result;
    for (int i = 0; i < this->toByteArray().size(); i++)
    {
        result.append(map[this->toByteArray()[i]]);
    }
    return result;
}

QString BigNumber::toString() const
{
    return positive ? this->hexValue : "-" + this->hexValue;
}

QByteArray BigNumber::toByteArray() const
{
    return toString().toLocal8Bit();
}

QByteArray BigNumber::serialize() const
{
    return toByteArray();
}

BigNumber BigNumber::abs() const
{
    BigNumber bigNumber = *this;
    bigNumber.setPositive(true);
    return bigNumber;
}

void BigNumber::setHexValue(const QString &hex)
{
    QByteArray num = cutZeros(hex).trimmed().toLocal8Bit();

    if (hex.isEmpty() || num.isEmpty())
        num = "0";

    if (num.startsWith("-"))
    {
        this->positive = false;
        this->hexValue = num.right(num.size() - 1);
    }
    else
    {
        this->hexValue = num;
    }

    if (hexValue == "0")
        this->positive = true;
}

void BigNumber::setPositive(bool newPositive)
{
    this->positive = newPositive;

    if (this->hexValue == "0")
        this->positive = true;
}

BigNumber BigNumber::pow(unsigned long long number) // naive
{
    BigNumber result = 1;
    for (unsigned long long i = 0; i != number; ++i)
        result = result * *this;
    return result;
}

QString BigNumber::toStringDec() const
{
    QString value = toDec(this->hexValue);
    return positive ? value : "-" + value;
}

void BigNumber::fromString(QString serialized)
{
    if (serialized.startsWith("-"))
    {
        QString val = serialized.mid(1, serialized.size());
        this->hexValue = val;
        this->setPositive(false);
    }
    else
    {
        this->hexValue = serialized;
        this->setPositive(true);
    }
}

BigNumber BigNumber::fromByteArray(QByteArray serialized)
{
    return BigNumber(serialized);
}

BigNumber BigNumber::factorial(int num) // naive
{
    BigNumber result = 1;

    for (int i = 2; i != num + 1; ++i)
        result *= i;

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

char BigNumber::binaryCompareAnd(char a, char b)
{
    if (a == '1' && b == '1')
        return '1';
    return '0';
}

BigNumber BigNumber::random(int n)
{
    const static std::vector<QChar> chars = { 'a', 'b', 'c', 'd', 'e', 'f', '0', '1',
                                              '2', '3', '4', '5', '6', '7', '8', '9' };
    QString str;
    str.reserve(n);
    str[0] = '0';

    while (str[0] == '0')
        str[0] = chars[std::size_t(QRandomGenerator::global()->bounded(16))];

    for (int i = 1; i != n; ++i)
        str[i] = chars[std::size_t(QRandomGenerator::global()->bounded(16))];

    return BigNumber(str.toLocal8Bit());
}

BigNumber BigNumber::random(int n, const BigNumber &max)
{
    if (max.getHexValue().length() < n)
        return BigNumber(0);

    BigNumber result;

    do
    {
        result = random(n);
    } while (result > max);

    return result;
}

QDataStream &operator<<(QDataStream &in, BigNumber &bigNumber)
{
    in << bigNumber.getHexValue();
    return in;
}

QDataStream &operator>>(QDataStream &out, BigNumber &bigNumber)
{
    QString val;
    out >> val;
    bigNumber.fromString(val);
    return out;
}

QDebug operator<<(QDebug debug, const BigNumber &bigNumber)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << bigNumber.toString();
    return debug;
}
