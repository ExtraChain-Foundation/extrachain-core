#ifndef PRIVATE_PROFILE_H
#define PRIVATE_PROFILE_H

#include <QByteArray>
#include "enc/algorithms/blowfish_crypt.h"
#include "utils/utils.h"
class AccountController;
class Dfs;
class PrivateProfile : public QObject
{
    Q_OBJECT
private:
    AccountController *acContorller;
    Dfs *dfs;
public slots:
    void savePrivateProfile(QByteArray login, QByteArray password, QByteArray id);
    void loadPrivateProfile(QByteArray login, QByteArray password);
    void editPrivateProfile(QByteArray hashLogin, QByteArray idProfile, QByteArray id);
    void loadProfileForAutoLogin(QByteArray hash);
    void process();

public:
    void setAccountController(AccountController *value);
    void setDfs(Dfs *value);

signals:
    void setIdProfile(QByteArray id);
    void setHashProfile(QByteArray hash);
    void initPrivateProfile(QByteArray id, QByteArrayList idList);

    void finished();

private:
    void profile(QByteArray hash);
};

#endif // PRIVATE_PROFILE_H
