#include "profile/public_profile.h"
PublicProfile::PublicProfile(Profile _profile, QByteArray _sign, QString path)
{
    profile = saveProfile(_profile, path, _sign);
    sign = _sign;
}

PublicProfile::PublicProfile(Profile _profile, QByteArray _sign)
{
    profile = _profile;
    sign = _sign;
}
PublicProfile::PublicProfile()
{
    profile = Profile();
    sign = "";
}
PublicProfile::PublicProfile(const QByteArray &serialize)
{
    int signSize = Utils::qByteArrayToInt(serialize.mid(serialize.size() - 4, 4));
    QByteArray _sign = serialize.mid(serialize.size() - signSize - 4, signSize);
    QByteArray data = serialize.mid(0, serialize.size() - signSize - 4);
    QByteArrayList list = deserialize(data);
    sign = _sign;
    profile = list;
}
QByteArray PublicProfile::serialize() const
{
    QByteArray data = serialize(profile.getConstList());
    QByteArray _sign = Serialization::universalSerialize({ sign }, 4);
    QByteArray signSize = _sign.mid(0, 4);
    _sign += signSize;
    _sign = _sign.mid(4, _sign.size());
    data += _sign;
    return data;
}

void PublicProfile::saveTokenNames(QByteArray id, QByteArray nameToken)
{
    QFile file("blockchain/.tokens");
    if (file.exists())
    {
        file.open(QIODevice::ReadOnly);
        QByteArray dataFromFile = file.readAll();
        QByteArrayList list = Serialization::universalDesirialize(dataFromFile, 4);
        for (int i = 0; i < list.size(); i = i + 2)
        {
            if (id == list.at(i))
            {
                file.flush();
                file.close();
                return;
            }
        }
        file.flush();
        file.close();
    }
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    QByteArray data = Serialization::universalSerialize({ id, nameToken }, 4);

    file.write(data);
    file.flush();
    file.close();
}

Profile PublicProfile::saveProfile(Profile newProfile, const QString &path, QByteArray sign)
{
    QByteArrayList &list = newProfile.list();
    QString pathProfile =
        path.mid(0, path.size() - newProfile.at(2).size()) + "profile/" + newProfile.at(2) + ".profile";
    QDir().mkdir(path.mid(0, path.size() - newProfile.at(2).size()) + "profile/");
    QFile profile(pathProfile);
    QByteArray serializeProfile = serialize(list);
    if (profile.exists())
    {
        profile.open(QIODevice::ReadOnly);
        QByteArray oldProfile = profile.readAll();
        profile.flush();
        profile.close();
        int signSize = Utils::qByteArrayToInt(oldProfile.mid(oldProfile.size() - 4, 4));
        oldProfile = oldProfile.mid(0, oldProfile.size() - 4 - signSize);
        if (serializeProfile == oldProfile)
        {
            qDebug() << "profile exist";
            return Profile();
        }
        else
            profile.resize(0);
    }
    QByteArray signWrite = Serialization::universalSerialize({ sign }, 4);
    QByteArray sign2 = signWrite.mid(4, signWrite.size());
    QByteArray signSize = signWrite.mid(0, 4);
    signWrite = sign2 + signSize;
    profile.open(QIODevice::WriteOnly);
    profile.write(serializeProfile + signWrite);
    profile.flush();
    profile.close();
#ifdef ETALONIUM_CLIENT
    if (newProfile.type() == 6)
        saveTokenNames(newProfile.list().at(2), newProfile.list().at(3));
#endif
    return newProfile;
}

PublicProfile PublicProfile::getProfile(const QString &path, const QString id)
{
    QDir().mkdir(path.mid(0, path.size() - id.size()) + "profile/");
    QString pathProfile = path.mid(0, path.size() - id.size()) + "profile/" + id + ".profile";
    QFile profile(pathProfile);
    if (!profile.exists())
    {
        // qDebug() << "Profile isn't exist" << id;
        return PublicProfile();
    }
    profile.open(QIODevice::ReadOnly);
    QByteArray serializeData = profile.readAll();
    profile.flush();
    profile.close();
    int signSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    QByteArray sign = serializeData.mid(serializeData.size() - 4 - signSize, signSize);
    serializeData = serializeData.mid(0, serializeData.size() - 4 - signSize);
    QByteArrayList listProfile = deserialize(serializeData);
    PublicProfile pubProfile(listProfile, sign);

    return pubProfile;
}

QByteArray PublicProfile::serialize(QByteArrayList actorList)
{
    QByteArray data = "";
    QByteArray actorData = "";
    uint count = 0;

    for (auto element : actorList)
    {
        if (count <= 2)
        {
            if (count > 0)
            {
                data = Serialization::universalSerialize({ element }, 4);
                actorData.append(data);
                data.clear();
                count++;
                continue;
            }
            actorData.append(element);
            count++;
            continue;
        }
        if (element == "")
        {
            data += "1| ";
            actorData.append(data);
            data.clear();
            continue;
        }
        data += QByteArray::number(element.size());
        data += "|";
        data += element;
        actorData.append(data);
        data.clear();
    }

    return actorData;
}

QByteArrayList PublicProfile::deserialize(QByteArray serializeData)
{
    QByteArrayList profileData;
    int position = 0, sizeField = 0;

    for (int i = 0; i < serializeData.size(); i++)
    {
        if (i == 0)
        {
            profileData.append(serializeData.mid(i, 1));
            ++position;
            continue;
        }
        if (i <= 2)
        {
            profileData.append(
                serializeData.mid(position + 4, Utils::qByteArrayToInt(serializeData.mid(position, 4))));
            position += 4 + Utils::qByteArrayToInt(serializeData.mid(position, 4));
            continue;
        }
        sizeField = Utils::qByteArrayToInt(
            serializeData.mid(position, serializeData.indexOf("|", position) - position));
        position += serializeData.mid(position, serializeData.indexOf("|", position) - position).size() + 1;
        if (serializeData.mid(position, sizeField) == " ")
        {
            profileData.append("");
            position += sizeField;
            i = position;
            continue;
        }
        profileData.append(serializeData.mid(position, sizeField));
        position += sizeField;
        i = position;
    }

    return profileData;
}

indexList::indexList(long long curPos, int _size)
{
    currentPosition = curPos;
    size = _size;
}
