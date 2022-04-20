#include "extrachainc.h"

#include <QCoreApplication>
#include <QObject>
#include <QThread>

#include <datastorage/actor.h>
#include <datastorage/index/actorindex.h>
#include <enc/enc_tools.h>
#include <managers/account_controller.h>
#include <managers/extrachain_node.h>
#include <managers/logs_manager.h>
#include <network/network_manager.h>
#include <profile/private_profile.h>

QCoreApplication *app;
ExtraChainNode *node;
QThread *m_thread;

void copy_char(char *&dest, const char *orig, size_t size) {
    // qDebug() << "[ExtraChainC] From" << orig << size;
    char *res = new char[size + 1];
    // char *res = (char *)malloc(size);
    memcpy(res, orig, size + 1);
    // res[size] = '\0';
    // qDebug() << "[ExtraChainC] After" << res << std::string(res).length();
    dest = res;
}

template <class T>
void copy_char(char *&dest, const T &str) {
    copy_char(dest, str.data(), str.size());
}

void copy_char(char *&dest, const QString &str) {
    QByteArray temp = str.toUtf8();
    copy_char(dest, temp.data(), temp.size());
}

ActorPrivate *actor_private_from_ActorKeyPrivate(const Actor<KeyPrivate> &actor) {
    if (actor.empty()) { } // TODO

    ActorPrivate *actor_private = new ActorPrivate;
    actor_private->type = int(actor.type());
    copy_char(actor_private->id, actor.id().toByteArray().data(), 20);
    copy_char(actor_private->secret_key, actor.key().secretKey().data(), 128);
    copy_char(actor_private->public_key, actor.key().publicKey().data(), 64);

    return actor_private;
}

ActorPublic *actor_public_from_ActorKeyPublic(const Actor<KeyPublic> &actor) {
    // TODO: checks

    ActorPublic *actor_public = new ActorPublic;
    actor_public->type = int(actor.type());
    copy_char(actor_public->id, actor.id().toByteArray().data(), 20);
    copy_char(actor_public->public_key, actor.key().publicKey().c_str(), 64);
    return actor_public;
}

Actor<KeyPrivate> ActorKeyPrivate_from_actor_private(const ActorPrivate *actor_private) {
    // TODO: checks

    Actor<KeyPrivate> actor;
    actor.setId(ActorId(std::string(actor_private->id)));
    actor.setType(ActorType(actor_private->type));
    actor.setSecretKey(actor_private->secret_key, actor_private->public_key);
    return actor;
}

Actor<KeyPublic> ActorKeyPublic_from_actor_public(const ActorPublic *actor_public) {
    // TODO: checks
    Actor<KeyPublic> actor;
    actor.setId(ActorId(std::string(actor_public->id)));
    actor.setType(ActorType(actor_public->type));
    actor.setPublicKey(actor_public->public_key);
    return actor;
}

void extrachain_free_char_str(const char *str) {
    delete[] str;
}

void extrachain_free_actor_private(ActorPrivate *actor_private) {
    delete[] actor_private->id;
    delete[] actor_private->secret_key;
    delete[] actor_private->public_key;
    delete actor_private;
}

void extrachain_free_actor_public(ActorPublic *actor_public) {
    delete[] actor_public->id;
    delete[] actor_public->public_key;
    delete actor_public;
}

void so_sleep(unsigned long secs) {
    qDebug() << "[ExtraChainC] Sleep secs:" << secs;
    QThread::sleep(secs);
}

char *extrachain_version() {
    char *version;
    copy_char(version, Utils::extrachainVersion());
    return version;
}

void extrachain_wipe() {
    qDebug() << "Wipe...";
    Utils::wipeDataFiles();
}

void extrachain_manage_logs(int log_type) {
    switch (log_type) {
    case 1:
        LogsManager::qtHandler();
        break;
    case 2:
        LogsManager::etHandler();
        break;
    case 3:
        LogsManager::etHandler();
        LogsManager::setDebugLogs(true);
        break;
    case 4:
        LogsManager::emptyHandler();
        break;
    }
}

void extrachain_init(int argc, char *argv[]) {
    // LogsManager::emptyHandler();
    // LogsManager::etHandler();
    app = new QCoreApplication(argc, argv);
    node = new ExtraChainNode;
    //    exc = new ExtraChainCWorker(argc, argv);
    m_thread = new QThread;
    app->moveToThread(m_thread);
    node->moveToThread(m_thread);
    QObject::connect(m_thread, &QThread::finished, app, &QCoreApplication::quit);
    m_thread->start();
    node->start();
    qDebug() << "[ExtraChainC] Finished extrachain_init";
}

void extrachain_stop() {
    node->deleteLater();
    m_thread->quit();
}

ActorPrivate *extrachain_create_actor(int type) {
    // TODO: add user, wallet functions
    if (type > 2 || type < 0) {
        qDebug() << "[ExtraChainC] Error type for creation actor";
        std::exit(0);
    }

    auto actor = node->accountController()->createUser(ActorType(type), node->privateProfile()->hash());

    ActorPrivate *actor_private = actor_private_from_ActorKeyPrivate(actor);

    return actor_private;
}

void extrachain_auth(char *login, char *password) {
    QString e = login;
    QString p = password;
    qDebug() << "[ExtraChainC] Auth with" << login << password;
    QByteArray hash = Utils::calcKeccak((e + p).toUtf8());
    node->privateProfile()->setHash(hash);
}

ActorPublic *extrachain_get_actor(char actor_id[]) {
    ActorId actorId = QString::fromLatin1(actor_id, 20).toStdString();
    auto actor = node->actorIndex()->getActor(actorId);

    if (actor.empty()) {
        ActorPublic *actor_public = new ActorPublic;
        actor_public->type = int(actor.type());
        return actor_public;
    }

    ActorPublic *actor_public = actor_public_from_ActorKeyPublic(actor);
    return actor_public;
}

bool extrachain_is_public_actor_valid(ActorPublic *actor_public) {
    return actor_public->type >= 0;
}

ActorPublic *extrachain_private_to_public(ActorPrivate *actor_private) {
    ActorPublic *actor_public = new ActorPublic;

    copy_char(actor_public->id, actor_private->id, 20);
    copy_char(actor_public->public_key, actor_private->public_key, 64);
    actor_public->type = actor_private->type;

    return actor_public;
}

void extrachain_login() {
    node->privateProfile()->loadPrivateProfileHash(node->privateProfile()->hash());
}

char *extrachain_sign(const char *data, size_t size, const ActorPrivate *actor_private) {
    auto actor = ActorKeyPrivate_from_actor_private(actor_private);
    auto sig = actor.key().sign(std::string(data, size));
    char *res;
    copy_char(res, sig.data(), sig.length());
    return res;
}

bool extrachain_verify_private(const char *data, size_t size, const char *sign,
                               const ActorPrivate *actor_private) {
    auto actor = ActorKeyPrivate_from_actor_private(actor_private);
    bool verify = actor.key().verify(std::string(data, size), sign);
    return verify;
}

bool extrachain_verify(const char *data, size_t size, const char *sign, const ActorPublic *actor_public) {
    auto actor = ActorKeyPublic_from_actor_public(actor_public);
    bool verify = actor.key().verify(QByteArray::fromRawData(data, size), sign);
    return verify;
}

char *extrachain_encrypt(const char *data, size_t size, const ActorPrivate *actor_private,
                         const ActorPublic *actor_public) {
    auto actorPrivate = ActorKeyPrivate_from_actor_private(actor_private);
    auto actorPublic = ActorKeyPublic_from_actor_public(actor_public);
    auto encrypted = SecretKey::encryptAsymmetric(std::string(data, size), actorPrivate.key().secretKey(),
                                                  actorPublic.key().publicKey());
    char *res;
    copy_char(res, encrypted);
    return res;
}

char *extrachain_decrypt(const char *data, size_t size, const ActorPrivate *actor_private,
                         const ActorPublic *actor_public) {
    auto actorPrivate = ActorKeyPrivate_from_actor_private(actor_private);
    auto actorPublic = ActorKeyPublic_from_actor_public(actor_public);
    auto decrypted = SecretKey::decryptAsymmetric(std::string(data, size), actorPrivate.key().secretKey(),
                                                  actorPublic.key().publicKey());
    char *res;
    copy_char(res, decrypted);
    return res;
}

char *extrachain_encrypt_self(const char *data, size_t size, const ActorPrivate *actor_private) {
    auto actorPrivate = ActorKeyPrivate_from_actor_private(actor_private);
    auto encrypted = actorPrivate.key().encryptSelf(std::string(data, size));
    char *res;
    copy_char(res, encrypted);
    return res;
}

char *extrachain_decrypt_self(const char *data, size_t size, const ActorPrivate *actor_private) {
    auto actorPrivate = ActorKeyPrivate_from_actor_private(actor_private);
    auto decrypted = actorPrivate.key().decryptSelf(std::string(data, size));
    char *res;
    copy_char(res, decrypted);
    return res;
}

void *extrachain_node_pointer() {
    return node;
}

void extrachain_network_connect(const char *ip, int type) {
    if (type != 1 && type != 2)
        return;

    node->network()->connectToNode(ip, Network::Protocol(type));
}

void extrachain_network_send(const char *data, size_t size) {
    // node->network()->send(data, 0, SocketPair(), Config::Net::TypeSend(0));
}
