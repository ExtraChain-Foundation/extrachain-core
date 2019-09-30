#ifndef PRIVATE_PROFILE_H
#define PRIVATE_PROFILE_H

#include <QByteArray>
#include "headers/crypt/xor_encrypt.h"
#include "utils/utils.h"
class PrivateProfile
{
public:
    void savePrivateProfile(QByteArray login, QByteArray password, QByteArray id);
    void editPrivateProfile(QByteArray login, QByteArray id);
    QByteArrayList loadPrivateProfile(QByteArray login, QByteArray password);
};

#endif // PRIVATE_PROFILE_H
