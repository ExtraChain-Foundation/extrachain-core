#include "bignumberdec.h"

BigNumberDec::BigNumberDec()
{
}

BigNumberDec::BigNumberDec(const QByteArray &bigNumber)
{
    this->setHexValue(bigNumber);
}

BigNumberDec::BigNumberDec(const BigNumberDec &other)
{
    this->setHexValue(other.getHexValue());
    this->setPositive(other.isPositive());
}

BigNumberDec::BigNumberDec(int number)
{
    if (number < 0)
    {
        number *= -1;
        this->positive = false;
    }

    hexValue = toHex(QString::number(number));
}

BigNumberDec::BigNumberDec(long long number)
{
    if (number < 0)
    {
        number *= -1;
        this->positive = false;
    }

    this->hexValue = toHex(QString::number(number));
}

BigNumberDec::~BigNumberDec()
{
}

QString BigNumberDec::toHex(const QString &dec) const
{
    qlonglong decVal = dec.toLongLong(nullptr, DEC_BASE);
    QString hexStr = QString::number(decVal, HEX_BASE);
    return hexStr;
}

QString BigNumberDec::toDec(const QString &hex) const
{
    qlonglong decVal = hex.toLongLong(nullptr, HEX_BASE);
    QString dec = QString::number(decVal, DEC_BASE);
    return dec;
}

QString BigNumberDec::fillZeros(const QString &number) const
{
    QString result = number;
    while (number.size() <= LONG_LONG_LENGTH)
    {
        result = "0" + result;
    }

    return result;
}

QString BigNumberDec::cutZeros(const QString &number) const
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

int BigNumberDec::compare(const QString &one, const QString &two)
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

BigNumberDec BigNumberDec::operator&(const BigNumberDec &value)
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
            BigNumberDec::binaryCompareAnd(this->toBinary()[mineSize + i], value.toBinary()[valueSize + i]));
    }
    //[mineSize-i-1]
    //[valueSize-i-1]
    return result;
}

std::pair<BigNumberDec, BigNumberDec> BigNumberDec::naiveDivide(BigNumberDec &value,
                                                                const BigNumberDec &divider)
{
    BigNumberDec result("0");
    while (value >= divider)
    {
        value = value - divider;
        ++result;
    }

    return std::make_pair(result, value);
}

std::pair<BigNumberDec, BigNumberDec> BigNumberDec::divide(BigNumberDec val, BigNumberDec divider)
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
    BigNumberDec mod;
    int last = 0;

    do
    {
        if (temp[0] == '0')
            temp = temp.remove(0, 1);

        QString num = value.mid(len, 1);
        BigNumberDec tempBig(temp.toLocal8Bit());
        BigNumberDec tempDivider(divider);
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

    BigNumberDec result(resultStr.toLocal8Bit());
    result.setPositive(resultPositive);

    mod.setHexValue(mod.hexValue);
    if (last < length - 1 && BigNumberDec(temp.toLocal8Bit()) > mod)
        mod.setHexValue(temp);
    mod.setPositive(modPositive);

    return std::make_pair(result, mod);
}

BigNumberDec BigNumberDec::operator+(const BigNumberDec &other)
{
    // some temp fixes for negative numbers
    if (!this->isPositive() && other.isPositive())
    {
        BigNumberDec one = *this;
        BigNumberDec two = other;
        one.positive = true;

        BigNumberDec result = one - two;
        result.positive = result.hexValue != "0" ? one < two : true;

        return result;
    }

    if (this->isPositive() && !other.isPositive())
    {
        BigNumberDec one = *this;
        BigNumberDec two = other;
        two.positive = true;

        BigNumberDec result = two - one;
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

    BigNumberDec result =
        (flag == 1 ? BigNumberDec(("1" + hex1).toLocal8Bit()) : BigNumberDec(hex1.toLocal8Bit()));

    if (!this->isPositive() && !other.isPositive())
        result.positive = !result.positive;

    if (result.hexValue == "0" || result.hexValue.isEmpty())
        result.setHexValue("0");

    return result;
}

BigNumberDec BigNumberDec::operator+(long long number)
{
    QByteArray hexVal = toHex(QString::number(number)).toLocal8Bit();
    return *this + BigNumberDec(hexVal);
}

BigNumberDec BigNumberDec::operator-(const BigNumberDec &bigNumber)
{
    if (!this->isPositive() && bigNumber.isPositive())
    {
        BigNumberDec one = *this;
        one.positive = true;

        BigNumberDec res = one + bigNumber;
        res.setPositive(false);

        return res;
    }

    if (this->isPositive() && !bigNumber.isPositive())
    {
        BigNumberDec two = bigNumber;
        two.positive = true;

        BigNumberDec res(*this + two);

        return res;
    }

    QString hex1 = this->hexValue;
    QString hex2 = bigNumber.getHexValue();
    BigNumberDec result;
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

BigNumberDec BigNumberDec::operator-(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this - BigNumberDec(hexVal.toLocal8Bit());
}

BigNumberDec BigNumberDec::operator*(const BigNumberDec &bigNumber)
{
    if (bigNumber.getHexValue() == "0" || this->hexValue == "0")
        return BigNumberDec();

    bool resultPositive = true;
    if (!this->isPositive() && !bigNumber.isPositive())
        resultPositive = true;
    else if (!this->isPositive() || !bigNumber.isPositive())
        resultPositive = false;

    BigNumberDec two = bigNumber;
    two.positive = true;

    QString multi = two.getHexValue();
    int length = multi.size();
    int digit = 0;

    BigNumberDec value = BigNumberDec(*this);
    value.positive = true;
    BigNumberDec result;

    while (length > 0)
    {
        int repeat = multi.mid(length - 1, 1).toInt(nullptr, HEX_BASE);
        BigNumberDec columnResult;

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

BigNumberDec BigNumberDec::operator*(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this * BigNumberDec(hexVal.toLocal8Bit());
}

BigNumberDec BigNumberDec::operator/(const BigNumberDec &divider)
{
    return BigNumberDec::divide(*this, divider).first;
}

BigNumberDec BigNumberDec::operator/(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this / BigNumberDec(hexVal.toLocal8Bit());
}

BigNumberDec BigNumberDec::operator%(const BigNumberDec &divider)
{
    return BigNumberDec::divide(*this, divider).second;
}

BigNumberDec BigNumberDec::operator%(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this % BigNumberDec(hexVal.toLocal8Bit());
}

BigNumberDec &BigNumberDec::operator=(const BigNumberDec &bigNumber)
{
    this->hexValue = bigNumber.getHexValue();
    this->setPositive(bigNumber.isPositive());
    return *this;
}

BigNumberDec &BigNumberDec::operator=(long long number)
{
    if (number < 0)
    {
        number *= -1;
        this->positive = false;
    }

    this->hexValue = toHex(QString::number(number));
    return *this;
}

BigNumberDec &BigNumberDec::operator++()
{
    *this = *this + 1;
    return *this;
}

BigNumberDec BigNumberDec::operator++(int)
{
    BigNumberDec val = *this;
    ++(*this);
    return val;
}

BigNumberDec &BigNumberDec::operator--()
{
    *this = *this - 1;
    return *this;
}

BigNumberDec BigNumberDec::operator--(int)
{
    BigNumberDec val = *this;
    --(*this);
    return val;
}

BigNumberDec &BigNumberDec::operator+=(const BigNumberDec &bigNumber)
{
    *this = *this + bigNumber;
    return *this;
}

BigNumberDec &BigNumberDec::operator+=(long long number)
{
    *this = *this + number;
    return *this;
}

BigNumberDec &BigNumberDec::operator-=(const BigNumberDec &bigNumber)
{
    *this = *this - bigNumber;
    return *this;
}

BigNumberDec &BigNumberDec::operator-=(long long number)
{
    *this = *this - number;
    return *this;
}

BigNumberDec &BigNumberDec::operator*=(const BigNumberDec &bigNumber)
{
    *this = *this * bigNumber;
    return *this;
}

BigNumberDec &BigNumberDec::operator*=(long long number)
{
    *this = *this * number;
    return *this;
}

BigNumberDec &BigNumberDec::operator/=(const BigNumberDec &bigNumber)
{
    *this = *this / bigNumber;
    return *this;
}

BigNumberDec &BigNumberDec::operator/=(long long number)
{
    *this = *this / number;
    return *this;
}

BigNumberDec &BigNumberDec::operator%=(const BigNumberDec &bigNumber)
{
    *this = *this % bigNumber;
    return *this;
}

BigNumberDec &BigNumberDec::operator%=(long long number)
{
    *this = *this % number;
    return *this;
}

BigNumberDec &BigNumberDec::operator-()
{
    this->setPositive(!positive);
    return *this;
}

bool BigNumberDec::isPrime() const
{
    BigNumberDec currentThis = *this;
    BigNumberDec n = sqrt(*this);

    if (*this == 2)
        return true;
    if (currentThis % 2 == 0 || *this == 1)
        return false;

    for (BigNumberDec i = 3; i <= n; i = i + 2)
    {
        if (currentThis % i == 0)
            return false;
    }

    return true;
}

bool BigNumberDec::isEmpty() const
{
    return *this == -1;
}

bool BigNumberDec::isPositive() const
{
    return this->positive;
}

QString BigNumberDec::getHexValue() const
{
    return this->hexValue;
}

QByteArray BigNumberDec::toBinary() const
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

QString BigNumberDec::toString() const
{
    return positive ? this->hexValue : "-" + this->hexValue;
}

QByteArray BigNumberDec::toByteArray() const
{
    return toString().toLocal8Bit();
}

QByteArray BigNumberDec::serialize() const
{
    return toByteArray();
}

BigNumberDec BigNumberDec::abs() const
{
    BigNumberDec bigNumber = *this;
    bigNumber.setPositive(true);
    return bigNumber;
}

void BigNumberDec::setHexValue(const QString &hex)
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

void BigNumberDec::setPositive(bool newPositive)
{
    this->positive = newPositive;

    if (this->hexValue == "0")
        this->positive = true;
}

BigNumberDec BigNumberDec::pow(unsigned long long number) // naive
{
    BigNumberDec result = 1;
    for (unsigned long long i = 0; i != number; ++i)
        result = result * *this;
    return result;
}

QString BigNumberDec::toStringDec() const
{
    QString value = toDec(this->hexValue);
    return positive ? value : "-" + value;
}

void BigNumberDec::fromString(QString serialized)
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

BigNumberDec BigNumberDec::fromByteArray(QByteArray serialized)
{
    return BigNumberDec(serialized);
}

BigNumberDec BigNumberDec::factorial(int num) // naive
{
    BigNumberDec result = 1;

    for (int i = 2; i != num + 1; ++i)
        result *= i;

    return result;
}

BigNumberDec BigNumberDec::sqrt(const BigNumberDec &value)
{
    BigNumberDec a("1");
    BigNumberDec b = value;
    BigNumberDec c = (a + b) / 2;

    while (b - c > BigNumberDec("1"))
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

char BigNumberDec::binaryCompareAnd(char a, char b)
{
    if (a == '1' && b == '1')
        return '1';
    return '0';
}

BigNumberDec BigNumberDec::random(int n)
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

    return BigNumberDec(str.toLocal8Bit());
}

BigNumberDec BigNumberDec::random(int n, const BigNumberDec &max)
{
    if (max.getHexValue().length() < n)
        return BigNumberDec(0);

    BigNumberDec result;

    do
    {
        result = random(n);
    } while (result > max);

    return result;
}

QDataStream &operator<<(QDataStream &in, BigNumberDec &bigNumber)
{
    in << bigNumber.getHexValue();
    return in;
}

QDataStream &operator>>(QDataStream &out, BigNumberDec &bigNumber)
{
    QString val;
    out >> val;
    bigNumber.fromString(val);
    return out;
}

QDebug operator<<(QDebug debug, const BigNumberDec &bigNumber)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << bigNumber.toString();
    return debug;
}
