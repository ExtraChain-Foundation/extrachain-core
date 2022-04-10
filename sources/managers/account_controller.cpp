/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "managers/account_controller.h"

#include <QDir>

#include "datastorage/blockchain.h"
#include "enc/enc_tools.h"
#include "profile/private_profile.h"

const std::vector<Actor<KeyPrivate>> &AccountController::accounts() const {
    return m_accounts;
}

AccountController::AccountController(ExtraChainNode *node) {
    this->node = node;
}

Actor<KeyPrivate> AccountController::createUser(ActorType account, QByteArray hashLogin) {
    if (hashLogin.isEmpty())
        qFatal("[AccountController] Create actor: hash is empty");

    Actor<KeyPrivate> actor;
    actor.create(account);
    m_accounts.push_back(actor);
    qDebug() << "[AccountController] Created" << actor;
    // emit verifyActor(actor.convertToPublic());

    node->actorIndex()->addActor(actor.convertToPublic());
    node->privateProfile()->setHash(hashLogin);
    savePrivateActor(actor, hashLogin);

    if (m_accounts.size() - 1 == 0) {
        emit savePrivateProfile(actor.id().toByteArray());
    }

    m_currentWallet = actor.id();

    qDebug() << "[AccountController] Create actor finished";

    node->start(); // TODO: remove

    if (!m_accounts.empty())
        node->blockchain()->getBlockZero();

    return actor;
}

Actor<KeyPrivate> AccountController::createWallet() {
    auto hash = node->privateProfile()->hash();
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    m_accounts.push_back(actor);
    node->actorIndex()->addActor(actor.convertToPublic());
    savePrivateActor(actor, hash);

    emit node->nodeEditPrivateProfile({ hash, node->accountController()->mainActor().id().toByteArray() },
                                      "wallet", actor.id().toByteArray(), false);

    return actor;
}

const Actor<KeyPrivate> &AccountController::getActor(const ActorId &id) {
    for (const Actor<KeyPrivate> &actor : qAsConst(m_accounts)) {
        if (id == actor.id()) {
            return actor;
        }
    }

    qFatal("Can't find actor with id");
    return mainActor();
}

const Actor<KeyPrivate> &AccountController::mainActor() {
    if (m_accounts.empty()) {
        qFatal("[AccountController] No main actor");
        std::exit(-1);
    }
    return m_accounts.front();
}

const Actor<KeyPrivate> &AccountController::currentWallet() {
    if (m_currentWallet == ActorId())
        qFatal("Incorrect current wallet");
    return getActor(m_currentWallet);
}

void AccountController::loadActors(const QByteArray &id, const QByteArrayList &idList,
                                   const QByteArray &hashLogin, const std::string &decryptKey) {
    if (id.isEmpty() || hashLogin.isEmpty()) {
        qDebug() << "[AccountController] Load actors: id or hashLogin is empty";
        Q_ASSERT(!id.isEmpty());
        Q_ASSERT(!hashLogin.isEmpty());
        return;
    }

    m_accounts.clear();
    qDebug() << "[AccountController] Attempting to load actors from local storage";
    QString path = KeyStore::USER_KEYSTORE;

    for (const QByteArray &fileName : idList) {
        QFile file(path + "/" + fileName + ".key");
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            QByteArray serialized =
                QByteArray::fromStdString(SecretKey::decrypt(file.readAll().toStdString(), decryptKey));
            qDebug() << serialized;
            file.close();
            if (!serialized.isEmpty()) {
                Actor<KeyPrivate> actor = Actor<KeyPrivate>::fromJson(serialized);
                qDebug().noquote() << "Actor" << actor.id()
                                   << "found locally:" << actor.key().secretKey().c_str();
                this->m_accounts.push_back(actor);
            }
        }
    }

    if (this->m_accounts.size() > 0) {
        qDebug() << "[AccountController]" << this->m_accounts.size() << "accounts have been loaded" << id;
        m_currentWallet = mainActor().id();
        node->blockchain()->getBlockZero();
        emit loadWallets(id, idList);
        node->start();
    } else {
        qDebug() << "[AccountController] There no accounts found locally";
    }
}

int AccountController::count() const {
    return m_accounts.size();
}

void AccountController::savePrivateActor(Actor<KeyPrivate> actor, QByteArray hashLogin) {
    qDebug() << "[AccountController] Attempting to save private actor" << actor.id();
    if (!m_accounts.empty())
        emit editPrivateProfile(actor.id().toByteArray());
    QString fileName = KeyStore::makeKeyFileName(actor.id().toByteArray());
    QString path = KeyStore::USER_KEYSTORE + fileName;
    qDebug() << "Path =" << path;

    QDir().mkpath(KeyStore::USER_KEYSTORE);
    QFile file(path);

    if (file.open(QIODevice::ReadWrite)) {
        QByteArray old = file.readAll();

        if (old == actor.toJson()) {
            qDebug() << "[AccountController] Private actor with id =" << actor.id() << "already exists";
        } else {
            qDebug().noquote() << "[AccountController] Actor serialized:" << actor.toJson();
            std::string hl = hashLogin.toStdString();
            file.write(
                QByteArray::fromStdString(SecretKey::encryptWithPassword(actor.toJson().toStdString(), hl)));
            file.flush();
            qDebug() << "[AccountController] Private actor" << actor.id() << "is successfully saved";
        }

        file.close();
        return;
    }

    qDebug() << "[AccountController] Can't save actor" << actor.id();
}

void AccountController::clearAcc() {
    m_accounts.clear();
    m_currentWallet = ActorId();
    qDebug() << "[AccountController] Cleared";
}

void AccountController::changeCurrentWallet(const ActorId &actorId) {
    if (!getActor(actorId).empty()) {
        m_currentWallet = actorId.toStdString();
    }
}
