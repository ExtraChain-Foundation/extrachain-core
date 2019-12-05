#include <profile/private_profile.h>
#include "managers/account_controller.h"
#include "dfs/controls/headers/dfs.h"

void PrivateProfile::setAccountController(AccountController *value)
{
    acContorller = value;
}

void PrivateProfile::setDfs(Dfs *value)
{
    dfs = value;
}

void PrivateProfile::savePrivateProfile(const QByteArray &hash, const QByteArray &id)
{
    QDir().mkdir(PathProfile);
    QMap<QString, QByteArray> map;
    set(map, "wallet", id);
    QByteArray data = "";
    writeData(map, data);
    data = hash + data;
    blowFish_crypt crypt;
    data = crypt.EncryptBlowFish(data, hash);
    QFile file(PathProfile + "/" + id + ".private");
    file.open(QIODevice::WriteOnly);
    file.write(data);
    file.flush();
    file.close();
}

void PrivateProfile::editPrivateProfile(const QByteArray &hashLogin, const QByteArray &idProfile,
                                        const QString &type, const QByteArray &_data, const bool &reWrite)
{
    QDir().mkdir(PathProfile);
    QFile file(PathProfile + "/" + idProfile + ".private");
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
        data = data.mid(64);
        QMap<QString, QByteArray> map;
        readData(map, data);
        if (reWrite)
            set(map, type, _data);
        else
            add(map, type, _data);
        data.clear();
        writeData(map, data);
        data = hashLogin + data;
        data = crypt.EncryptBlowFish(data, hashLogin);
        file.resize(0);
        file.write(data);
    }
    else
        qDebug() << "[PrivateProfile] Error : incorrect login or id";
    file.flush();
    file.close();
}

void PrivateProfile::loadInfoFromPrivateProfile(const QByteArray &hash, const QByteArray &idProfile,
                                                const QString &type)
{
    QFile file(PathProfile + "/" + idProfile + ".private");
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.flush();
    file.close();
    blowFish_crypt crypt;
    data = crypt.DecryptBlowFish(data, hash);
    QByteArray secureLoginFile = data.mid(0, 64);
    if (secureLoginFile == hash)
    {
        data = data.mid(64);
        QMap<QString, QByteArray> map;
        readData(map, data);
        QByteArray info = get(map, type);
        emit infoToUi(info, type);
        return;
    }
    else
        qDebug() << "[PrivateProfile] Error : incorrect login or id";
    return;
}

void PrivateProfile::loadPrivateProfile(const QByteArray &login, const QByteArray &password)
{
    QByteArray data = login + password;
    QByteArray secureLogin = Utils::calcKeccak(data);
    profile(secureLogin);
}
void PrivateProfile::loadProfileForAutoLogin(const QByteArray &hash)
{
    profile(hash);
}

void PrivateProfile::process()
{
}

void PrivateProfile::profile(const QByteArray &hash)
{
    QDir dir(PathProfile);
    QStringList users = dir.entryList(QDir::Files);

    if (users.isEmpty())
    {
        qDebug() << "ERROR: empty keystore";
        return;
    }
    else
    {
        for (QString &fileName : users)
        {
            QFile file(PathProfile + "/" + fileName);
            file.open(QIODevice::ReadOnly);
            QByteArray data = file.readAll();
            file.flush();
            file.close();
            blowFish_crypt crypt;
            data = crypt.DecryptBlowFish(data, hash);
            QByteArray secureLoginFile = data.mid(0, 64);
            if (secureLoginFile == hash)
            {
                data = data.mid(64);
                emit setHashProfile(secureLoginFile);
                QMap<QString, QByteArray> map;
                readData(map, data);
                QList<QByteArray> idList = get(map, "wallet").split('|');
                emit setIdProfile(idList.first());
                qDebug() << "Load private profile with id" << idList.first();
                acContorller->loadActors(idList.first(), idList);
                if (acContorller->getMainActor() != nullptr)
                    dfs->init();
                emit initActorChatM();
            }
            else
            {
#ifdef ETALONIUM_CONSOLE
                qInfo() << "Incorrect password";
                std::exit(0);
#endif
                continue;
            }
        }
    }
    return;
}
QByteArray PrivateProfile::get(QMap<QString, QByteArray> &map, const QString &value)
{
    return map[value];
}

void PrivateProfile::set(QMap<QString, QByteArray> &map, const QString &value, const QByteArray &data)
{
    map[value] = data;
}

void PrivateProfile::add(QMap<QString, QByteArray> &map, const QString &value, const QByteArray &data)
{
    if (!map[value].contains(data))
        map[value] += "|" + data;
}

void PrivateProfile::writeData(QMap<QString, QByteArray> &map, QByteArray &out)
{
    QMap<QString, QByteArray>::iterator it = map.begin();
    QByteArray res = "";
    while (it != map.end())
    {
        res += Serialization::universalSerialize({ it.key().toUtf8(), it.value() });
        it++;
    }
    out = res;
}

void PrivateProfile::readData(QMap<QString, QByteArray> &map, QByteArray &data)
{
    QByteArrayList res;
    res = Serialization::universalDeserialize(data);
    while (res.size() != 0)
    {
        map.insert(res.at(0), res.at(1));
        res.removeFirst();
        res.removeFirst();
    }
}
