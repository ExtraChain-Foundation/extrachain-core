#include <profile/private_profile.h>

void PrivateProfile::savePrivateProfile(QByteArray login, QByteArray password, QByteArray id)
{
    QDir().mkdir("keystore/profile");
    QByteArray data = login + password;
    QByteArray secureLogin = Utils::calcKeccak(data);
    data = secureLogin + "|" + id;
    blowFish_crypt crypt;
    data = crypt.EncryptBlowFish(data, secureLogin);
    QFile file("keystore/profile/" + id + ".private");
    file.open(QIODevice::WriteOnly);
    file.write(data);
    file.flush();
    file.close();
    emit setIdProfile(id);
    emit setHashProfile(secureLogin);
}
void PrivateProfile::editPrivateProfile(QByteArray hashLogin, QByteArray idProfile, QByteArray id)
{
    QDir().mkdir("keystore/profile");
    QFile file("keystore/profile/" + idProfile + ".private");
    if (!file.exists())
    {
        qDebug() << "Don`t have private profile";
        return;
    }
    file.open(QIODevice::ReadWrite);
    QByteArray data = file.readAll();
    blowFish_crypt crypt;
    data = crypt.DecryptBlowFish(data, hashLogin);
    if (data.mid(0, 64) == hashLogin)
    {
        data.append("|" + id);
        data = crypt.EncryptBlowFish(data, hashLogin);
        file.resize(0);
        file.write(data);
    }
    else
        qDebug() << "Error : incorrect login or id";
    file.flush();
    file.close();
}

void PrivateProfile::loadPrivateProfile(QByteArray login, QByteArray password)
{
    QByteArray data = login + password;
    QByteArray secureLogin = Utils::calcKeccak(data);
    profile(secureLogin);
}
void PrivateProfile::loadProfileForAutoLogin(QByteArray hash)
{
    profile(hash);
}

void PrivateProfile::profile(QByteArray hash)
{
    QDir().mkdir("keystore/profile");
    QDir dir("keystore/profile");
    QStringList users = dir.entryList(QDir::Files);
    QByteArrayList idList;
    if (users.isEmpty())
    {
        qDebug() << "ERROR: empty keystore";
        return;
    }
    else
    {
        for (QString &fileName : users)
        {
            QFile file("keystore/profile/" + fileName);
            file.open(QIODevice::ReadOnly);
            QByteArray data = file.readAll();
            file.flush();
            file.close();
            blowFish_crypt crypt;
            data = crypt.DecryptBlowFish(data, hash);
            QByteArray secureLoginFile = data.mid(0, 64);
            data = data.mid(64, data.size());
            if (secureLoginFile == hash)
            {
                emit setHashProfile(secureLoginFile);
                int position = 0;
                while (position < data.size())
                {
                    position = data.indexOf("|", position) + 1;
                    if (position == 0)
                        break;
                    QByteArray id = data.mid(position, data.indexOf("|", position) - position);
                    idList.append(id);
                }
                emit setIdProfile(idList.first());

                qDebug() << "Load private profile with id" << idList.first();
                emit sendPrivateProfile(idList.first(), idList);
            }
            else
                continue;
        }
    }
    return;
}
