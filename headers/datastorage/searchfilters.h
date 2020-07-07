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

#ifndef SEARCHFILTERS_H
#define SEARCHFILTERS_H

#include <QObject>
#include <QVariantMap>
#include <QDebug>

struct SearchFilters
{
    Q_GADGET
    Q_PROPERTY(QString currentId MEMBER currentId)
    Q_PROPERTY(int userType MEMBER userType)
    Q_PROPERTY(QString name MEMBER name)
    Q_PROPERTY(int minAge MEMBER minAge)
    Q_PROPERTY(int maxAge MEMBER maxAge)
    Q_PROPERTY(int location MEMBER location)
    Q_PROPERTY(int gender MEMBER gender)
    Q_PROPERTY(int heightMin MEMBER heightMin)
    Q_PROPERTY(int heightMax MEMBER heightMax)
    Q_PROPERTY(int bustMin MEMBER bustMin)
    Q_PROPERTY(int bustMax MEMBER bustMax)
    Q_PROPERTY(int waistMin MEMBER waistMin)
    Q_PROPERTY(int waistMax MEMBER waistMax)
    Q_PROPERTY(int hipsMin MEMBER hipsMin)
    Q_PROPERTY(int hipsMax MEMBER hipsMax)
    Q_PROPERTY(int shoesMin MEMBER shoesMin)
    Q_PROPERTY(int shoesMax MEMBER shoesMax)

    Q_PROPERTY(QList<int> category MEMBER category)
    Q_PROPERTY(QList<int> body MEMBER body)
    Q_PROPERTY(QList<int> hair MEMBER hair)
    Q_PROPERTY(QList<int> hairLength MEMBER hairLength)
    Q_PROPERTY(QList<int> eye MEMBER eye)
    Q_PROPERTY(QList<int> sizes MEMBER sizes)
    Q_PROPERTY(QList<int> ethnicity MEMBER ethnicity)
    Q_PROPERTY(QList<int> style MEMBER style)
    Q_PROPERTY(QList<int> sports MEMBER sports)
    Q_PROPERTY(QList<int> skin MEMBER skin)
    Q_PROPERTY(QList<int> scope MEMBER scope)
    Q_PROPERTY(QList<int> direction MEMBER direction)
    Q_PROPERTY(QList<int> workStyle MEMBER workStyle)
    Q_PROPERTY(QList<int> fashion MEMBER fashion)

public:
    QString currentId;
    int userType;
    QString name;
    int minAge;
    int maxAge;
    int location;
    int gender;
    int heightMin;
    int heightMax;
    int bustMin;
    int bustMax;
    int waistMin;
    int waistMax;
    int hipsMin;
    int hipsMax;
    int shoesMin;
    int shoesMax;
    QList<int> category;
    QList<int> body;
    QList<int> hair;
    QList<int> hairLength;
    QList<int> eye;
    QList<int> sizes;
    QList<int> ethnicity;
    QList<int> style;
    QList<int> sports;
    QList<int> skin;
    QList<int> scope;
    QList<int> direction;
    QList<int> workStyle;
    QList<int> fashion;

public:
    operator QString() const // for QDebug
    {
        return QString("currentId: %1, userType: %2, name: '%3', minAge: %4, maxAge: %5, location: %6, "
                       "gender: %7, fashion: %8, heightMin: %9, heightMax: %10, bustMin: %11, bustMax: %12, "
                       "waistMin: %13, waistMax: %14, hipsMin: %15, hipsMax: %16, shoesMin: %17, shoesMax: "
                       "%18, categoryModel: %19, scope: %20, bodyType: %21, hairType: %22, hairLength: %23, "
                       "eyeType: %24, ethnicity: %25, style: %26, sports: %27, skin: %28, fashion: %29")
            .arg(currentId)               // 1
            .arg(userType)                // 2
            .arg(name)                    // 3
            .arg(minAge)                  // 4
            .arg(maxAge)                  // 5
            .arg(location)                // 6
            .arg(gender)                  // 7
            .arg(fromListInt(fashion))    // 8
            .arg(heightMin)               // 9
            .arg(heightMax)               // 10
            .arg(bustMin)                 // 11
            .arg(bustMax)                 // 12
            .arg(waistMin)                // 13
            .arg(waistMax)                // 14
            .arg(hipsMin)                 // 15
            .arg(hipsMax)                 // 16
            .arg(shoesMin)                // 17
            .arg(shoesMax)                // 18
            .arg(fromListInt(category))   // 19
            .arg(fromListInt(scope))      // 20
            .arg(fromListInt(body))       // 21
            .arg(fromListInt(hair))       // 22
            .arg(fromListInt(hairLength)) // 23
            .arg(fromListInt(eye))        // 24
            .arg(fromListInt(ethnicity))  // 25
            .arg(fromListInt(style))      // 26
            .arg(fromListInt(sports))     // 27
            .arg(fromListInt(skin))       // 28
            ;
    }

private:
    QString fromListInt(const QList<int> &list) const
    {
        QStringList result;

        for (int el : list)
            result << QString::number(el);

        return result.join(" ");
    }
};

#endif // SEARCHFILTERS_H
