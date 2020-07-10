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

#include "profile/profile.h"

Profile::Profile()
{
    setType(1);
    setVersion(1);
}

Profile::Profile(const QByteArrayList &list)
{
    m_list = list;
}

Profile::Profile(const QByteArray &serialize)
{
    //    this->m_list = PublicProfile::deserialize(serialize);
}

Profile::Profile(const Profile &profile)
{
    this->m_list = profile.m_list;
}

QByteArray Profile::serialize() const
{
    //    return PublicProfile::serialize(m_list);
    return "";
}

QString Profile::toString() const
{
    return m_list.join(" | ");
}

QByteArrayList &Profile::list()
{
    return m_list;
}

const QByteArrayList Profile::getConstList() const
{
    return m_list;
}

void Profile::setList(const QByteArrayList &value)
{
    m_list = value;
}

Profile::operator QString() const // for QDebug
{
    return toString();
}

void Profile::setValue(const QString &field, const QByteArray &value)
{
    int index = this->field(field);

    if (index < 0 || index > m_list.length() - 1)
    {
        qDebug() << "Profile list: \"index out of range\" for field" << field << "and value" << value;
        return;
    }

    m_list[index] = value;
}

bool Profile::isEmpty()
{
    bool isEmpty = true;

    // qDebug() << m_list.length();
    for (int i = 2; i < m_list.length(); ++i)
    {
        if (!at(i).isEmpty())
        {
            // qDebug() << i;
            isEmpty = false;
            break;
        }
    }

    return isEmpty;
}

QString Profile::value(const QString &field) const
{
    int index = this->field(field);
    // qDebug() << index << this->field(field);
    return at(index);
}

QByteArray Profile::at(int index) const
{
    if (index < 0 || index > m_list.length() - 1)
        return "";

    return m_list[index];
}

int Profile::field(const QString &field) const
{
    int type = at(0).toInt();
    int index = 0;

    switch (type)
    {
    case 1:
        index = fieldsCustomer.indexOf(field);
        break;
    case 2:
        index = fieldsModel.indexOf(field);
        break;
    case 3:
        index = fieldsAgent.indexOf(field);
        break;
    case 4:
        index = fieldsAgency.indexOf(field);
        break;
    case 5:
        index = fieldsFashion.indexOf(field);
        break;
    }

    return index;
}

QString Profile::typeString()
{
    switch (type())
    {
    case 1:
        return "Customer";
    case 2:
        return "Model";
    case 3:
        return "Agent";
    case 4:
        return "Agency";
    case 5:
        return "Fashion Maker";
    default:
        return "Unknown";
    }
}

qint16 Profile::type() const
{
    return qint16(value("type").toInt());
}

QString Profile::userId() const
{
    return value("userId");
}

QString Profile::firstName() const
{
    if (at(0) == "6")
        return "Token " + at(3);

    return value("firstName");
}

QString Profile::lastName() const
{
    return value("lastName");
}

qint64 Profile::registerDate() const
{
    return value("registerDate").toLongLong();
}

qint16 Profile::birthDay() const
{
    return qint16(value("birthday").left(2).toInt());
}

qint16 Profile::birthMonth() const
{
    return qint16(value("birthday").mid(2, 2).toInt());
}

qint16 Profile::birthYear() const
{
    return qint16(value("birthday").right(4).toInt());
}

qint16 Profile::gender() const
{
    return qint16(value("gender").toInt());
}

QStringList Profile::avatar() const
{
    QString avatar = value("avatar").trimmed();
    if (avatar.isEmpty())
        return {};
    return avatar.split(" ");
}

qint16 Profile::country() const
{
    return qint16(value("country").toInt());
}

QString Profile::bio() const
{
    return value("bio");
}

QString Profile::url() const
{
    return value("url");
}

QString Profile::urlName() const
{
    return value("urlName");
}

QString Profile::facebook() const
{
    return value("facebook");
}

QString Profile::instagram() const
{
    return value("instagram");
}

QString Profile::ethereum() const
{
    return value("ethereum");
}

QStringList Profile::portfolio()
{
    QString portfolio = value("portfolio").trimmed();
    if (portfolio.isEmpty())
        return {};
    return portfolio.split(" ");
}

bool Profile::unit()
{
    return value("unit").toInt();
}

QList<int> Profile::category()
{
    return toListInt("category");
}

QList<int> Profile::body()
{
    return toListInt("body");
}

QList<int> Profile::hair()
{
    return toListInt("hair");
}

QList<int> Profile::hairLength()
{
    return toListInt("hairLength");
}

QList<int> Profile::eye()
{
    return toListInt("eye");
}

QList<int> Profile::sizes()
{
    return toListInt("sizes");
}

QList<int> Profile::ethnicity()
{
    return toListInt("ethnicity");
}

QList<int> Profile::style()
{
    return toListInt("style");
}

QList<int> Profile::sports()
{
    return toListInt("sports");
}

QList<int> Profile::skin()
{
    return toListInt("skin");
}

QList<int> Profile::scope()
{
    return toListInt("scope");
}

QList<int> Profile::direction()
{
    return toListInt("direction");
}

QList<int> Profile::workStyle()
{
    return toListInt("workStyle");
}

QList<int> Profile::fashion()
{
    return toListInt("fashion");
}

QStringList Profile::pubs()
{
    return value("pubs").split(" ");
}

void Profile::setUserId(const QString &userId)
{
    setValue("userId", userId.toUtf8());
}

void Profile::setVersion(int version)
{
    setValue("version", QByteArray::number(version));
}

void Profile::setType(qint16 type)
{
    if (this->type() == type)
        return;

    int length = 0;

    switch (type) // TODO
    {
    case 1:
        length = fieldsCustomer.length();
        break;
    case 2:
        length = fieldsModel.length();
        break;
    case 3:
        length = fieldsAgent.length();
        break;
    case 4:
        length = fieldsAgency.length();
        break;
    case 5:
        length = fieldsFashion.length();
        break;
    case 6:
        length = fieldsToken.length();
        break;
    }

    int diff = length - m_list.size();
    QByteArray ba;
    if (diff > 0)
    {
        m_list.reserve(length);
        while (diff--)
            m_list.append(ba);
    }
    else if (diff < 0)
        m_list.erase(m_list.end() + diff, m_list.end());

    setValue("type", QByteArray::number(type));
}

void Profile::setFirstName(const QString &firstName)
{
    setValue("firstName", firstName.toUtf8());
}

void Profile::setLastName(const QString &lastName)
{
    setValue("lastName", lastName.toUtf8());
}

void Profile::setRegisterDate(qint64 registerDate)
{
    setValue("registerDate", QByteArray::number(registerDate));
}

void Profile::setBirthDay(qint16 birthDay)
{
    QString birthday = value("birthday").right(2 + 4);
    QString dayString = QString::number(birthDay);
    if (dayString.length() == 1)
        dayString = "0" + dayString;
    setValue("birthday", (dayString + birthday).toLatin1());
}

void Profile::setBirthMonth(qint16 birthMonth)
{
    QString birthday = value("birthday");
    QString birthday1 = birthday.left(2);
    QString birthday2 = birthday.right(4);
    QString monthString = QString::number(birthMonth);
    if (monthString.length() == 1)
        monthString = "0" + monthString;
    setValue("birthday", (birthday1 + monthString + birthday2).toLatin1());
}

void Profile::setBirthYear(qint16 birthYear)
{
    QString birthday = value("birthday").left(2 + 2);
    setValue("birthday", (birthday + QString::number(birthYear)).toLatin1());
}

void Profile::setGender(qint16 gender)
{
    setValue("gender", QByteArray::number(gender));
}

void Profile::setAvatar(QStringList avatar)
{
    setValue("avatar", avatar.join(" ").toUtf8());
}

void Profile::setBio(const QString &bio)
{
    setValue("bio", bio.toUtf8());
}

void Profile::setUrl(const QString &url)
{
    setValue("url", url.toUtf8());
}

void Profile::setUrlName(const QString &urlName)
{
    setValue("urlName", urlName.toUtf8());
}

void Profile::setCountry(qint16 country)
{
    setValue("country", QByteArray::number(country));
}

void Profile::setFacebook(const QString &facebook)
{
    setValue("facebook", facebook.toUtf8());
}

void Profile::setInstagram(const QString &instagram)
{
    setValue("instagram", instagram.toUtf8());
}

void Profile::setEthereum(const QString &ethereum)
{
    setValue("ethereum", ethereum.toUtf8());
}

void Profile::setPortfolio(QStringList portfolio)
{
    setValue("portfolio", portfolio.join(" ").toUtf8());
}

void Profile::setUnit(bool unit)
{
    setValue("unit", QByteArray::number(unit));
}

void Profile::setCategory(QList<int> category)
{
    setValue("category", fromListInt(category).toUtf8());
}

void Profile::setBody(QList<int> body)
{
    setValue("body", fromListInt(body).toUtf8());
}

void Profile::setHair(QList<int> hair)
{
    setValue("hair", fromListInt(hair).toUtf8());
}

void Profile::setHairLength(QList<int> hairLength)
{
    setValue("hairLength", fromListInt(hairLength).toUtf8());
}

void Profile::setEye(QList<int> eye)
{
    setValue("eye", fromListInt(eye).toUtf8());
}

void Profile::setSizes(QList<int> sizes)
{
    setValue("sizes", fromListInt(sizes).toUtf8());
}

void Profile::setEthnicity(QList<int> ethnicity)
{
    setValue("ethnicity", fromListInt(ethnicity).toUtf8());
}

void Profile::setStyle(QList<int> style)
{
    setValue("style", fromListInt(style).toUtf8());
}

void Profile::setSports(QList<int> sports)
{
    setValue("sports", fromListInt(sports).toUtf8());
}

void Profile::setSkin(QList<int> skin)
{
    setValue("skin", fromListInt(skin).toUtf8());
}

void Profile::setScope(QList<int> scope)
{
    setValue("scope", fromListInt(scope).toUtf8());
}

void Profile::setDirection(QList<int> direction)
{
    setValue("direction", fromListInt(direction).toUtf8());
}

void Profile::setWorkStyle(QList<int> workStyle)
{
    setValue("workStyle", fromListInt(workStyle).toUtf8());
}

void Profile::setFashion(QList<int> fashion)
{
    setValue("fashion", fromListInt(fashion).toUtf8());
}

bool Profile::isServiceExists(ServiceCheckType checkType)
{
    static QStringList files = { "chatinvite" };

    if (checkType == ServiceCheckType::Full && files.length() == 1)
    {
        files << "follower"
              << "subscribe";
    }

    QString servicePath = QString("%1/%2/services/").arg(DfsStruct::ROOT_FOOLDER_NAME, userId());

    for (const QString &file : files)
    {
        QString original = servicePath + file;
        QString stored = original + DfsStruct::STORED_EXT;
        if (!QFile::exists(original) || !QFile::exists(stored))
            return false;
    }

    return true;
}

QList<int> Profile::toListInt(const QString &field)
{
    if (field.isEmpty())
        return {};

    QList<int> list;
    QStringList stringList = value(field).split(" ");
    for (QString &car : stringList)
        list << qint16(car.toInt());
    return list;
}

QString Profile::fromListInt(const QList<int> &list)
{
    QStringList result;

    for (int el : list)
        result << QString::number(el);

    return result.join(" ");
}

//
const QStringList Profile::fieldsCustomer = { "type",     "version",      "userId",    "firstName",
                                              "lastName", "registerDate", "birthday",  "gender",
                                              "avatar",   "country",      "bio",       "url",
                                              "urlName",  "facebook",     "instagram", "ethereum" };

const QStringList Profile::fieldsModel = fieldsCustomer
    + QStringList({ "unit", "category", "body", "hair", "hairLength", "eye", "sizes", "ethnicity", "style",
                    "sports", "skin", "scope", "portfolio" });
const QStringList Profile::fieldsAgent = fieldsCustomer + QStringList { "direction", "workStyle" };
const QStringList Profile::fieldsAgency = fieldsCustomer + QStringList { "direction", "workStyle" };
const QStringList Profile::fieldsFashion =
    fieldsCustomer + QStringList { "direction", "workStyle", "fashion", "portfolio" };
const QStringList Profile::fieldsToken = QStringList { "type", "version", "userId" };

//
const QMap<int, QString> Profile::unitMap { { 0, "Imperial" }, { 1, "Metric" } };
const QMap<int, QString> Profile::categoryMap { { 0, "Editorial" },  { 1, "Mature" }, { 2, "Child" },
                                                { 3, "Commercial" }, { 4, "Petite" }, { 5, "Pregnant" },
                                                { 6, "Fitness" },    { 7, "Plus" } };
const QMap<int, QString> Profile::bodyMap {
    { 0, "Petit" }, { 1, "Average" }, { 2, "Thin" }, { 3, "Athletic" }, { 4, "Heavy" }
};
const QMap<int, QString> Profile::hairMap {
    { 0, "Blonde" },    { 1, "Dark Blonde" }, { 2, "Light Blonde" },
    { 3, "Brown" },     { 4, "Dark Brown" },  { 5, "Black" },
    { 6, "Auburn" },    { 7, "Red" },         { 8, "Strawberry Blonde" },
    { 9, "Gray/White" }
};
const QMap<int, QString> Profile::hairLengthMap {
    { 0, "Long" },     { 1, "Short" }, { 2, "Shoulder length" }, { 3, "Chin length" }, { 4, "Shaved short" },
    { 5, "Receding" }, { 6, "Bald" }
};
const QMap<int, QString> Profile::eyeMap { { 0, "Blue" },  { 1, "Green" }, { 2, "Blue Green" },
                                           { 3, "Brown" }, { 4, "Hazel" }, { 5, "Gray" },
                                           { 6, "Black" } };
const QMap<int, QString> Profile::ethnicityMap { { 0, "Middle East" },    { 1, "Native American" },
                                                 { 2, "Latin American" }, { 3, "Causian" },
                                                 { 4, "Mixed" },          { 5, "African" },
                                                 { 6, "Indian" },         { 7, "Asian" },
                                                 { 8, "Aboriginal" },     { 9, "Polynesian" } };
const QMap<int, QString> Profile::styleMap { { 0, "White" },       { 1, "Blue" },   { 2, "Artist" },
                                             { 3, "Alternative" }, { 4, "Techie" }, { 5, "Quirky" } };

const QMap<int, QString> Profile::sportsMap { { 0, "Skiing" },     { 1, "Sailing" },  { 2, "Tennis" },
                                              { 3, "Bicycling" },  { 4, "Aerobics" }, { 5, "Skateboarding" },
                                              { 6, "Basketball" }, { 7, "Baseball" }, { 8, "Soccer" },
                                              { 9, "Football" },   { 10, "Running" }, { 11, "Yoga" } };
const QMap<int, QString> Profile::skinMap {
    { 0, "Tattoo" }, { 1, "Brand" }, { 2, "Piercing" }, { 3, "Scar" }
};
const QMap<int, QString> Profile::scopeMap { { 0, "Fashion" }, { 1, "Act" },      { 2, "Portrait" },
                                             { 3, "Glamour" }, { 4, "Lingerie" }, { 5, "Event" },
                                             { 6, "Hand" } };
const QMap<int, QString> Profile::directionMap { { 0, "White-color" }, { 1, "Blue-color" }, { 2, "Art" },
                                                 { 3, "Act" },         { 4, "Portrait" },   { 5, "Glamour" },
                                                 { 6, "Fashion" },     { 7, "Event" } };
const QMap<int, QString> Profile::workStyleMap { { 0, "White-color" }, { 1, "Blue-color" }, { 2, "Art" },
                                                 { 3, "Act" },         { 4, "Portrait" },   { 5, "Glamour" },
                                                 { 6, "Fashion" },     { 7, "Event" } };
const QMap<int, QString> Profile::fashionMap {
    { 0, "Photographer" }, { 1, "Affiliate" }, { 2, "Designer" }, { 3, "Visagiste" }, { 4, "Stylist" }
};
