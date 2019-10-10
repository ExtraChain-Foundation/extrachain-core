#include "utils/bignumber.h"

#ifdef QT_DEBUG
#include <QRegularExpression>
#endif

BigNumber::BigNumber()
{
}

BigNumber::BigNumber(const QByteArray &bigNumber, int base)
{
    this->setBase(base);
    this->setHexValue(bigNumber);
}

BigNumber::BigNumber(const BigNumber &other)
{
    this->setHexValue(other.getHexValue());
    this->setPositive(other.isPositive());
    this->setBase(other.getBase());
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

int BigNumber::getBase() const
{
    return base;
}

void BigNumber::setBase(int value)
{
    base = value;
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
    //    QByteArray a = this->toBase(2).toByteArray();
    //    QByteArray b = value.toBase(2).toByteArray();
    //    while (a.length() > b.length())
    //    {
    //        b.push_front("0");
    //    }
    //    while (a.length() < b.length())
    //    {
    //        a.push_front("0");
    //    }
    //    QByteArray c;
    //    for (int i = 0; i < a.length(); i++)
    //    {
    //        if ((a[i] == '1') && (b[i] == '1'))
    //            c.append('1');
    //        else
    //            c.append('0');
    //    }
    //    BigNumber res(c, 2);
    //    return res.toBase(16);
    //    BigNumber qwerty(toBase(2), 2);

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
    return BigNumber(result, 2).toBase(16);
}

BigNumber BigNumber::operator>>(const int &value)
{
    BigNumber n = this->toBase(2);
    QByteArray b = n.toByteArray();
    b.chop(value);
    return BigNumber(b, 2).toBase(16);
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
        one.setBase(base);
        two.setBase(base);
        one.positive = true;

        BigNumber result = one - two;
        result.setBase(base);
        result.positive = result.hexValue != "0" ? one < two : true;

        return result;
    }

    if (this->isPositive() && !other.isPositive())
    {
        BigNumber one = *this;
        BigNumber two = other;
        one.setBase(base);
        two.setBase(base);
        two.positive = true;

        BigNumber result = two - one;
        result.setBase(base);
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
        get1 = hex1.mid(length1 - 1, 1).toInt(nullptr, base);

        // get second number
        if (length2 > 0)
            get2 = hex2.mid(length2 - 1, 1).toInt(nullptr, base);
        else
            get2 = 0;

        // get the sum
        sum = get1 + get2 + flag;

        if (sum >= base)
        {
            int left = sum % base;
            hex1[length1 - 1] = QString::number(left, base).at(0);
            flag = 1;
        }
        else
        {
            hex1[length1 - 1] = QString::number(sum, base).at(0);
            flag = 0;
        }

        length1--;
        length2--;
    }

    BigNumber result = (flag == 1 ? BigNumber(("1" + hex1).toLocal8Bit()) : BigNumber(hex1.toLocal8Bit()));
    result.setBase(base);

    if (!this->isPositive() && !other.isPositive())
        result.positive = !result.positive;

    if (result.hexValue == "0" || result.hexValue.isEmpty())
        result.setHexValue("0");

    return result;
}

BigNumber BigNumber::operator+(long long number)
{
    QByteArray hexVal = toHex(QString::number(number)).toLocal8Bit();
    return *this + BigNumber(hexVal, base);
}

BigNumber BigNumber::operator-(const BigNumber &bigNumber)
{
    if (!this->isPositive() && bigNumber.isPositive())
    {
        BigNumber one = *this;
        one.setBase(base);
        one.positive = true;

        BigNumber res = one + bigNumber;
        res.setBase(base);
        res.setPositive(false);

        return res;
    }

    if (this->isPositive() && !bigNumber.isPositive())
    {
        BigNumber two = bigNumber;
        two.setBase(base);
        two.positive = true;

        BigNumber res(*this + two);
        res.setBase(base);
        return res;
    }

    QString hex1 = this->hexValue;
    QString hex2 = bigNumber.getHexValue();
    BigNumber result;
    result.setBase(base);
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
        get1 = hex1.mid(length1 - 1, 1).toInt(nullptr, base);

        // get second number
        if (length2 > 0)
            get2 = hex2.mid(length2 - 1, 1).toInt(nullptr, base);
        else
            get2 = 0;

        div = get1 - get2 - flag;

        if (div < 0)
        {
            int convertVal = base + get1 - flag;
            convertVal = convertVal - get2;
            res_s.insert(0, QString::number(convertVal, base).at(0));
            // hex1[length1-1] = QString::number(convertVal, HEX_BASE).at(0);
            flag = 1;
        }
        else
        {
            res_s.insert(0, QString::number(div, base).at(0));
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
    return *this - BigNumber(hexVal.toLocal8Bit(), base);
}

BigNumber BigNumber::operator*(const BigNumber &bigNumber)
{
    if (bigNumber.getHexValue() == "0" || this->hexValue == "0")
    {
        BigNumber empty;
        empty.setBase(base);
        return empty;
    }

    bool resultPositive = true;
    if (!this->isPositive() && !bigNumber.isPositive())
        resultPositive = true;
    else if (!this->isPositive() || !bigNumber.isPositive())
        resultPositive = false;

    BigNumber two = bigNumber;
    two.setBase(base);
    two.positive = true;

    QString multi = two.getHexValue();
    int length = multi.size();
    int digit = 0;

    BigNumber value(*this);
    value.setBase(base);
    value.positive = true;
    BigNumber result;
    result.setBase(base);

    while (length > 0)
    {
        int repeat = multi.mid(length - 1, 1).toInt(nullptr, base);
        BigNumber columnResult;
        columnResult.setBase(base);

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
    return *this * BigNumber(hexVal.toLocal8Bit(), base);
}

BigNumber BigNumber::operator/(const BigNumber &divider)
{
    return BigNumber::divide(*this, divider).first;
}

BigNumber BigNumber::operator/(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this / BigNumber(hexVal.toLocal8Bit(), base);
}

BigNumber BigNumber::operator%(const BigNumber &divider)
{
    return BigNumber::divide(*this, divider).second;
}

BigNumber BigNumber::operator%(long long number)
{
    QString hexVal = toHex(QString::number(number));
    return *this % BigNumber(hexVal.toLocal8Bit(), base);
}

BigNumber &BigNumber::operator=(const BigNumber &bigNumber)
{
    this->hexValue = bigNumber.getHexValue();
    this->setPositive(bigNumber.isPositive());
    this->setBase(bigNumber.getBase());
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
    val.setBase(base);
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
{ // TODO: base
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
    return *this == -1; // || hexValue.isEmpty();
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
    // QByteArray num = cutZeros(hex).trimmed().toLocal8Bit();
    //#ifdef QT_DEBUG
    //    static QRegularExpression regExp("[a-f0-9]+");
    //    if (hex.length() && !regExp.match(hex).hasMatch())
    //    {
    //        std::cout << "BigNumber error for hex" << hex.toStdString() << std::endl;
    //        std::exit(-1);
    //    }
    //#endif
    QByteArray num = cutZeros(hex).trimmed().toUtf8();
    //    QByteArray num = hex.toUtf8();
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
    result.setBase(base);
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

BigNumber BigNumber::toBase(int to) const
{
    return BigNumber::fromBase(toByteArray(), base, to);
}

BigNumber BigNumber::fromByteArray(QByteArray serialized, int base)
{
    return BigNumber(serialized, base);
}

BigNumber BigNumber::factorial(int num, int base) // naive
{
    BigNumber result = 1;
    result.setBase(base);

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
    } while (result >= max);

    return result;
}

BigNumber BigNumber::random(const BigNumber &max)
{
    BigNumber t = max.toBase(10);
    t.setBase(10);
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

BigNumber BigNumber::fromDec(const QByteArray &dec)
{
    return fromBase(dec, 10, 16);
}

BigNumber BigNumber::fromBase(QByteArray hexValue, int from, int base)
{
    if (base < 2 || base > 36)
        return BigNumber();

    BigNumber res(0);
    res.setBase(base);
    if (hexValue.isEmpty())
        return BigNumber("0");
    bool positive = hexValue.at(0) != '-';
    unsigned long j = 0;
    BigNumber two(QByteArray::number(from, base));
    two.setBase(base);

    for (int i = hexValue.length() - 1; i >= 0 + !positive; i--)
    {
        BigNumber one(QByteArray::number(QString(hexValue.at(i)).toInt(nullptr, from), base));
        one.setBase(base);

        BigNumber result = 1;
        result.setBase(base);
        for (unsigned long long i = 0; i != j; ++i)
            result = result * two;
        j++;

        res += one * result;
    }

    if (!positive)
        res.setPositive(false);

    return res;
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
    debug.nospace().noquote() << "\"0x" << bigNumber.toString() << "\"";
    return debug;
}
