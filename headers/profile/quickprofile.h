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

#ifndef QUICKPROFILE_H
#define QUICKPROFILE_H

#include <QObject>
#include <QVariantMap>
#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

struct QuickProfile
{
    Q_GADGET
    Q_PROPERTY(QString firstName MEMBER firstName)
    Q_PROPERTY(QString lastName MEMBER lastName)
    Q_PROPERTY(QString avatar MEMBER avatar)

public:
    QString userId;
    QString firstName;
    QString lastName;
    QString avatar;

    operator QString() const // for QDebug
    {
        return QString("first name: %1, last name: %2, avatar: %3").arg(firstName, lastName, avatar);
    }
};

#endif // QUICKPROFILE_H
