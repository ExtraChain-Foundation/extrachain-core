#ifndef CRYPT_MANAGER_H
#define CRYPT_MANAGER_H
#include <QByteArray>
#include "datastorage/actor.h"
#include "enc/key_public.h"
#include "enc/key_private.h"
#include "managers/account_controller.h"
#include <QObject>
namespace crypting {

static const int ENCRYPT_USE_KEY_REQUEST = 701;
static const int ENCRYPT_USE_ACTOR_REQUEST = 702;
static const int DECRYPT_USE_KEY__REQUEST = 704;
static const int DECRYPT_USE_ACTOR__REQUEST = 705;
static const int DECRYPT_USE_MAIN_ACTOR_REQUEST = 706;

static const int DECRYPT_DATA_RESPONSE = 707;
static const int ENCRYPT_DATA_RESPONSE = 708;

static const int SIG_IN_PAGE = 11;
static const int LOG_IN_PAGE = 12;
static const int AUTO_LOG_IN_PAGE = 13;
}
class CryptManager : public QObject
{
    Q_OBJECT
private:
    AccountController *accountControler;

public:
    CryptManager(AccountController *accountControler, QObject *parent = nullptr);
    ~CryptManager();

private:
    QByteArray encrypt(const QByteArray &data, const BigNumber &actorId);
    QByteArray encrypt(const QByteArray &data, const QByteArray &key);
    QByteArray decrypt(const QByteArray &data);                        // TODO: For chat
    QByteArray decrypt(const QByteArray &data, const QByteArray &key); // TODO: For chat
    QByteArray decrypt(const QByteArray &data, const BigNumber &actorId);
signals:
    // for thread
    void finished();
    // main functional
    void sendEncryptData(int place, int answer, QByteArray data);

public slots:
    // for thread
    void process();
    // main functional
    void recieveData(int place, int request, QByteArray data, QByteArray key);
};

#endif // CRYPT_MANAGER_H
