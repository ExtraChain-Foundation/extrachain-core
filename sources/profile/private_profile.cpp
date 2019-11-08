#include <profile/private_profile.h>
#include "headers/managers/account_controller.h"
#include "dfs/controls/headers/dfs.h"

void PrivateProfile::setAccountController(AccountController *value)
{
    acContorller = value;
}

void PrivateProfile::setDfs(Dfs *value)
{
    dfs = value;
}

void PrivateProfile::savePrivateProfile(QByteArray login, QByteArray password, QByteArray id)
{
    QDir().mkdir("keystore/profile");
    QByteArray data = login + password;
    QByteArray secureLogin = Utils::calcKeccak(data);
    data = secureLogin + id;
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
void PrivateProfile::editPrivateProfile(QByteArray hashLogin, QByteArray idProfile, QByteArray _data,
                                        typeDataPrProfile type)
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
        data = data.mid(64);
        QByteArrayList listAll = data.split('|');
        if (type == 0)
        {
            QByteArrayList listId = listAll.at(0).split('/');
            if (!listId.contains(_data))
            {
                QByteArray res = "";
                if (listAll.size() > 1)
                    res = listAll.at(0) + "/" + _data + "|" + listAll.at(1);
                else
                    res = listAll.at(0) + "/" + _data + "|";
                res.insert(0, hashLogin);
                res = crypt.EncryptBlowFish(res, hashLogin);
                file.resize(0);
                file.write(res);
            }
        }
        else
        {
            QByteArray res = "";
            if (listAll.size() > 1)
            {
                res = listAll.at(0) + "|" + _data;
                res.insert(0, hashLogin);
                res = crypt.EncryptBlowFish(res, hashLogin);
                file.resize(0);
                file.write(res);
            }
            else
            {
                qDebug() << "Don`t have interests in prProfile" << idProfile;
            }
        }
    }
    else
        qDebug() << "Error : incorrect login or id";
    file.flush();
    file.close();
}

void PrivateProfile::loadInterestsFromPrivateProfile(QByteArray hash, QByteArray idProfile)
{
    QFile file("keystore/profile/" + idProfile + ".private");
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
        QByteArrayList listAll = data.split('|');
        if (listAll.size() < 1)
            qDebug() << "No interests in PrProfile" << idProfile;
        QByteArray interes = listAll.at(1);
        emit interestsToUi(interes);
        return;
    }
    return;
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

void PrivateProfile::process()
{
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
            if (secureLoginFile == hash)
            {
                data = data.mid(64);
                emit setHashProfile(secureLoginFile);
                QByteArrayList listAll = data.split('|');
                idList = listAll.at(0).split('/');
                emit setIdProfile(idList.first());
                qDebug() << "Load private profile with id" << idList.first();
                acContorller->loadActors(idList.first(), idList);
                if (acContorller->getMainActor() != nullptr)
                    dfs->init();
                //                emit initPrivateProfile(idList.first(), idList);
            }
            else
                continue;
        }
    }
    return;
}
