#ifndef PRIVATE_PROFILE_H
#define PRIVATE_PROFILE_H

#include <QByteArray>
#include "enc/algorithms/blowfish_crypt.h"
#include "utils/utils.h"
class PrivateProfile : public QObject
{
    Q_OBJECT
public slots:
    void savePrivateProfile(QByteArray login, QByteArray password, QByteArray id);
    void loadPrivateProfile(QByteArray login, QByteArray password);
    void editPrivateProfile(QByteArray hashLogin, QByteArray idProfile, QByteArray id);
    void loadProfileForAutoLogin(QByteArray hash);
    void process();

signals:
    void setIdProfile(QByteArray id);
    void setHashProfile(QByteArray hash);
    void sendPrivateProfile(QByteArray id, QByteArrayList idList);
    void finished();

private:
    void profile(QByteArray hash);
};

#endif // PRIVATE_PROFILE_H
