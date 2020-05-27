#include "profile/public_profile.h"

PublicProfile::PublicProfile(QByteArrayList _profile, QByteArray _sign, QString path, QByteArray _id)
{
    sign = _sign;
    id = _id;
    setProfile(_profile, path);
}

PublicProfile::PublicProfile()
{
    idPath = "";
    sign = "";
    id = "";
}

PublicProfile::PublicProfile(QByteArray _id, QString _path)
{
    id = _id;
    idPath = _path + id + ".profile";

    QFile profile(idPath);
    if (!profile.exists())
        return;
    profile.open(QIODevice::ReadOnly);

    QByteArray serializeData = profile.readAll();
    profile.flush();
    profile.close();
    int signSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    sign = serializeData.mid(serializeData.size() - 4 - signSize, signSize);
}

PublicProfile::PublicProfile(const QByteArray &serialize)
{
    int signSize = Utils::qByteArrayToInt(serialize.mid(serialize.size() - 4, 4));
    id = serialize.mid(serialize.size() - signSize - 4, signSize);
    QByteArray serializeData = serialize.mid(0, serialize.size() - signSize - 4);
    signSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    sign = serializeData.mid(serializeData.size() - signSize - 4, signSize);
    QByteArray data = serializeData.mid(0, serializeData.size() - signSize - 4);
    int pathSize = Utils::qByteArrayToInt(data.mid(data.size() - 4, 4));
    idPath = data.mid(data.size() - 4 - pathSize, pathSize);
    QByteArray dataToProfile = data.mid(0, data.size() - 4 - pathSize);
    saveProfileFromNet(dataToProfile);
}

QByteArray PublicProfile::serialize() const
{
    QFile profile(idPath);
    profile.open(QIODevice::ReadOnly);
    QByteArray data = profile.readAll() + idPath.toLatin1() + Utils::intToByteArray(idPath.size(), 4) + sign
        + Utils::intToByteArray(sign.size(), 4) + id + Utils::intToByteArray(id.size(), 4);
    profile.flush();
    profile.close();
    return data;
}

void PublicProfile::setProfile(QByteArrayList profile, QString path)
{
    idPath = path;
    QFile file(path);
    QByteArray newProfile = serialize(profile);
    QByteArray signWrite = sign + Utils::intToByteArray(sign.size(), 4);
    file.open(QIODevice::WriteOnly);
    file.resize(0);
    file.write(newProfile + signWrite);
    file.flush();
    file.close();

    if (newProfile.mid(0, 1) == "6")
    {
        QByteArrayList list = deserialize(newProfile);
        saveTokenNames(list.at(2), list.at(3), list.at(6));
    }
}

void PublicProfile::saveTokenNames(QByteArray id, QByteArray nameToken, QByteArray color)
{
    DBConnector db("blockchain/tokens.cache");
    db.createTable(Config::DataStorage::tokensCacheTableCreate);
    db.insert(Config::DataStorage::tokensCacheTable,
              { { "tokenId", id.toStdString() },
                { "name", nameToken.toStdString() },
                { "color", color.toStdString() },
                { "canStaking", "1" } }); // TODO
}

void PublicProfile::saveProfileFromNet(QByteArray newProfile)
{
    QDir().mkdir(DfsStruct::ROOT_FOOLDER_NAME + "/" + id + "/profile/");
    QFile profile(idPath);
    if (profile.exists())
    {
        profile.open(QIODevice::ReadOnly);
        QByteArray oldProfile = profile.readAll();
        profile.flush();
        profile.close();
        if (newProfile == oldProfile)
        {
            qDebug() << "profile exist";
            sign = "";
            return;
        }
        else
            profile.resize(0);
    }
    profile.open(QIODevice::WriteOnly);
    profile.write(newProfile);
    profile.flush();
    profile.close();

    if (newProfile.mid(0, 1) == "6")
    {
        int signSize = Utils::qByteArrayToInt(newProfile.mid(newProfile.size() - 4, 4));
        QByteArray sign = newProfile.mid(newProfile.size() - 4 - signSize, signSize);
        QByteArray serializeData = newProfile.mid(0, newProfile.size() - 4 - signSize);
        QByteArrayList list = deserialize(serializeData);
        saveTokenNames(list.at(2), list.at(3), list.at(6));
    }
}

QByteArrayList PublicProfile::getListProfile()
{
    // QDir().mkdir(idPath);
    QString pathProfile = idPath;
    QFile profile(pathProfile);
    if (!profile.exists())
    {
        // qDebug() << "Profile isn't exist" << id;
        return { QByteArrayList() };
    }
    profile.open(QIODevice::ReadOnly);
    QByteArray serializeData = profile.readAll();
    profile.flush();
    profile.close();
    int signSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    QByteArray sign = serializeData.mid(serializeData.size() - 4 - signSize, signSize);
    serializeData = serializeData.mid(0, serializeData.size() - 4 - signSize);
    QByteArrayList listProfile = deserialize(serializeData);

    return listProfile;
}

QByteArray PublicProfile::getProfile()
{
    QString pathProfile = idPath;
    QFile profile(pathProfile);
    if (!profile.exists())
    {
        // qDebug() << "Profile isn't exist" << id;
        return { "" };
    }
    profile.open(QIODevice::ReadOnly);
    QByteArray serializeData = profile.readAll();
    profile.flush();
    profile.close();
    int signSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    QByteArray sign = serializeData.mid(serializeData.size() - 4 - signSize, signSize);
    serializeData = serializeData.mid(0, serializeData.size() - 4 - signSize);

    return serializeData;
}

QByteArray PublicProfile::serialize(QByteArrayList profileList)
{
    QByteArray data = "";
    QByteArray actorData = "";
    int count = -1;
    QByteArray index = "";
    for (QByteArray element : profileList)
    {
        if (count == -1)
        {
            actorData.append(element);
            count++;
            continue;
        }
        if (count == 0)
        {
            index += "0 1 4|";
            actorData.append(Utils::intToByteArray(element.toInt(), 4));
            count++;
            continue;
        }
        if (element == "")
        {
            index += QByteArray::number(count) + " 0|";
            actorData.append(element);
            count++;
            continue;
        }
        index += QByteArray::number(count) + " " + QByteArray::number(actorData.size()) + " "
            + QByteArray::number(element.size()) + "|";
        actorData.append(element);
        count++;
    }
    return actorData + "|" + index + Utils::intToByteArray(index.size(), 4);
}

QByteArrayList PublicProfile::deserialize(QByteArray serializeData)
{
    QByteArrayList profileData;
    int size = 0, pos = 0;
    profileData.append(serializeData.mid(0, 1));
    int indexSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    QByteArray index = serializeData.mid(serializeData.size() - 4 - indexSize, indexSize);
    while (index.size() > 1)
    {
        QByteArray index1 = index.mid(0, index.indexOf("|"));
        index = index.mid(index1.size() + 1);
        if (index1.contains(" 0"))
        {
            profileData.append("");
            continue;
        }
        index1 = index1.mid(index1.indexOf(" ") + 1);
        pos = Utils::qByteArrayToInt(index1.mid(0, index1.indexOf(" ")));
        size = Utils::qByteArrayToInt(index1.mid(index1.indexOf(" ")));
        profileData.append(serializeData.mid(pos, size));
    }

    return profileData;
}

QByteArrayList PublicProfile::getQuickProfile(QByteArray _data)
{
    //    QString pathProfile = idPath;
    //    QDir().mkdir(idPath);
    //    QFile profile(pathProfile);
    QByteArrayList list;
    //    profile.open(QIODevice::ReadOnly);
    QByteArray serializeData = _data;
    //    profile.flush();
    //    profile.close();
    int indexSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    QByteArray index = serializeData.mid(serializeData.size() - 4 - indexSize, indexSize);
    serializeData = serializeData.mid(0, serializeData.size() - 4 - indexSize);
    int pos = 0, size = 0;
    for (int i = 1; i < 4; i++)
    {
        int pos1 = index.indexOf("|" + QByteArray::number(i) + " ");
        QByteArray index1 =
            index.mid(index.indexOf("|" + QByteArray::number(i) + " "), index.indexOf("|", pos1 + 1) - pos1);
        if (index1.contains(" 0"))
        {
            list.append("");
            continue;
        }
        index1 = index1.mid(index1.indexOf(" ") + 1);
        pos = Utils::qByteArrayToInt(index1.mid(0, index1.indexOf(" ")));
        size = Utils::qByteArrayToInt(index1.mid(index1.indexOf(" ")));
        list.append(serializeData.mid(pos, size));
    }
    int pos1 = index.indexOf("|" + QByteArray::number(7) + " ");
    QByteArray index1 =
        index.mid(index.indexOf("|" + QByteArray::number(7) + " "), index.indexOf("|", pos1 + 1) - pos1);
    if (index1.contains(" 0"))
    {
        list.append("");
        return list;
    }
    index1 = index1.mid(index1.indexOf(" ") + 1);
    pos = Utils::qByteArrayToInt(index1.mid(0, index1.indexOf(" ")));
    size = Utils::qByteArrayToInt(index1.mid(index1.indexOf(" ")));
    list.append(serializeData.mid(pos, size));
    return list;
}

indexList::indexList(long long curPos, int _size)
{
    currentPosition = curPos;
    size = _size;
}
