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
    void savePrivateProfile(const QByteArray &hash, const QByteArray &id);
    void loadPrivateProfile(const QByteArray &login, const QByteArray &password);
    void editPrivateProfile(const QByteArray &hashLogin, const QByteArray &idProfile, const QString &type,
                            const QByteArray &_data, const bool &reWrite);
    void loadInfoFromPrivateProfile(const QByteArray &hash, const QByteArray &idProfile, const QString &type);
    void loadProfileForAutoLogin(const QByteArray &hash);
    void process();

public:
    void setAccountController(AccountController *value);
    void setDfs(Dfs *value);

signals:
    void setIdProfile(QByteArray id);
    void setHashProfile(QByteArray hash);
    void initActorChatM();
    void infoToUi(const QByteArray &info, const QString &type);

    void finished();

private:
    QByteArray get(QMap<QString, QByteArray> &map, const QString &value);
    void set(QMap<QString, QByteArray> &map, const QString &value, const QByteArray &data);
    void add(QMap<QString, QByteArray> &map, const QString &value, const QByteArray &data);
    void profile(const QByteArray &hash);

    template <typename T>
    void writeData(T &val, QByteArray &out)
    {
        QDataStream in(&out, QIODevice::WriteOnly);
        in << val;
    }

    template <typename T>
    void readData(T &val, QByteArray &data)
    {
        QDataStream out(&data, QIODevice::ReadOnly);
        out >> val;
    }

    const QString PathProfile = "keystore/profile";
};

#endif // PRIVATE_PROFILE_H
