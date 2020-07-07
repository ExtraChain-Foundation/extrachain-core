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

#ifndef PUBLIC_PROFILE_H
#define PUBLIC_PROFILE_H

#include "profile/profile.h"
#include "utils/utils.h"
#include "utils/db_connector.h"

struct indexList
{
    indexList(long long curPos, int _size);
    long long currentPosition;
    int size;
};

class PublicProfile
{
public:
    PublicProfile(QByteArrayList _profile, QByteArray _sign, QString path, QByteArray _id);
    PublicProfile(const QByteArray &serialize);
    PublicProfile(QByteArray _id, QString _path);
    PublicProfile();
    QByteArray serialize() const;
    void setProfile(QByteArrayList profile, QString path);
    void saveProfileFromNet(QByteArray newProfile);
    QByteArrayList getListProfile();
    QByteArray getProfile();
    static QByteArray serialize(QByteArrayList profileList);
    static QByteArrayList deserialize(QByteArray serializeData);
    QByteArrayList getQuickProfile(QByteArray _data);
    QByteArray sign = "";
    QString idPath;
    QByteArray id;

signals:
    //
private:
    static void saveTokenNames(QByteArray id, QByteArray nameToken, QByteArray color);
    //    QList<indexList> index;
};
#endif // PUBLIC_PROFILE_H
