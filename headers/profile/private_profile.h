#ifndef PRIVATE_PROFILE_H
#define PRIVATE_PROFILE_H

#include <QByteArray>
#include "enc/algorithms/blowfish_crypt.h"
#include "utils/utils.h"
class AccountController;
class Dfs;
enum typeDataPrProfile
{
    WALLETS = 0,
    INTERESTS = 1
};
class PrivateProfile : public QObject
{
    Q_OBJECT
private:
    AccountController *acContorller;
    Dfs *dfs;
public slots:
    void savePrivateProfile(QByteArray hash, QByteArray id);
    void loadPrivateProfile(QByteArray login, QByteArray password);
    void editPrivateProfile(QByteArray hashLogin, QByteArray idProfile, QByteArray data,
                            typeDataPrProfile type);
    void loadInterestsFromPrivateProfile(QByteArray hash, QByteArray idProfile);
    void loadProfileForAutoLogin(QByteArray hash);
    void process();

public:
    void setAccountController(AccountController *value);
    void setDfs(Dfs *value);

signals:
    void setIdProfile(QByteArray id);
    void setHashProfile(QByteArray hash);
    void initPrivateProfile(QByteArray id, QByteArrayList idList);
    void interestsToUi(QByteArray interes);

    void finished();

private:
    void profile(QByteArray hash);
};

#endif // PRIVATE_PROFILE_H
