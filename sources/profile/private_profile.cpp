#include <profile/private_profile.h>

void PrivateProfile::savePrivateProfile(QByteArray login, QByteArray password, QByteArray id)
{
    QByteArray data = login + password;
    QByteArray secureLogin = Utils::calcKeccak(data);
    data = secureLogin + "/n" + id;
    data = XOREncrypt::encrypt(secureLogin, data);
    QDir().mkdir("keystore");
    QFile file("keystore/" + id + ".private");
    file.open(QIODevice::WriteOnly);
    file.write(data);
    file.flush();
    file.close();
}
void PrivateProfile::editPrivateProfile(QByteArray login, QByteArray id)
{

    QFile file("keystore/" + id + ".private");
    file.open(QIODevice::ReadWrite);
    QByteArray _login = file.readLine();
    _login = XOREncrypt::decrypt(login, _login);
    if (_login == login)
    {
        file.write("/n" + id);
    }
    else
        qDebug() << "Error : incorrect login or id";
    file.flush();
    file.close();
}

QByteArrayList PrivateProfile::loadPrivateProfile(QByteArray login, QByteArray password)
{
    QByteArray data = login + password;
    QByteArray secureLogin = Utils::calcKeccak(data);
    QDir dir("keystore");
    QStringList users = dir.entryList(QDir::Files);
    QByteArrayList idList;
    if (users.isEmpty())
    {
        return QByteArrayList();
    }
    else
    {
        foreach (QString fileName, users)
        {
            QFile file("keystore/" + fileName);
            file.open(QIODevice::ReadOnly);
            QByteArray data = file.readAll();
            data = XOREncrypt::decrypt(secureLogin, data);
            if (data.mid(0, 64) == secureLogin)
            {
                file.seek(63);
                while (file.pos() < file.size())
                {
                    idList.append(file.readLine());
                }
                file.flush();
                file.close();
                return idList;
            }
            else
            {
                file.flush();
                file.close();
                continue;
            }
        }
    }
    return idList;
}
