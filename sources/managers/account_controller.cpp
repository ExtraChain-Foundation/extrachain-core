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

const QList<Actor<KeyPrivate>> &AccountController::accounts() const {
    return m_accounts;
}

QList<ActorId> AccountController::getListAccounts() const {
    QList<ActorId> res;
    for (const auto &tmp : qAsConst(m_accounts)) {
        res.append(tmp.id());
    }
    return res;
}

AccountController::AccountController(ExtraChainNode *node) {
    this->node = node;
    // when private actor is verified by actor index -> save it locally
    // connect(actorIndex, &ActorIndex::PrivateActorIsVerified, this, &AccountController::savePrivateActor);
    //    if (!QFile(KeyStore::user_actor_state).exists())
    //    {
    //        QFile file(KeyStore::user_actor_state);
    //        file.open(QIODevice::WriteOnly);
    //        file.flush();
    //        file.close();
    //    }
    // loadActors();
}

QList<QByteArray> AccountController::getAccountID() {
    QList<QByteArray> list;
    for (const auto &account : qAsConst(m_accounts)) {
        list << account.id().toByteArray();
    }
    return list;
}

Actor<KeyPrivate> AccountController::createUser(ActorType account, QByteArray hashLogin) {
    if (hashLogin.isEmpty())
        qFatal("[AccountController] Create actor: hash is empty");

    Actor<KeyPrivate> actor;
    actor.create(account);
    m_accounts << actor;
    qDebug() << "[AccountController] Created" << actor;
    // emit verifyActor(actor.convertToPublic());

    node->actorIndex()->addActor(actor.convertToPublic());
    node->privateProfile()->setHash(hashLogin);
    savePrivateActor(actor, hashLogin);

    if (m_accounts.size() - 1 == 0) {
        emit savePrivateProfile(actor.id().toByteArray());
    }

    userNum = m_accounts.size() - 1;

    qDebug() << "[AccountController] Create actor finished";

    node->start(); // TODO: remove

    if (!m_accounts.isEmpty())
        node->blockchain()->getBlockZero();

    return actor;
}

Actor<KeyPrivate> AccountController::createWallet() {
    auto hash = node->privateProfile()->hash();
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    m_accounts << actor;
    node->actorIndex()->addActor(actor.convertToPublic());
    savePrivateActor(actor, hash);

    emit node->nodeEditPrivateProfile({ hash, node->accountController()->mainActor().id().toByteArray() },
                                      "wallet", actor.id().toByteArray(), false);

    return actor;
}

Actor<KeyPrivate> AccountController::getActor(const ActorId &id) {
    for (const Actor<KeyPrivate> &actor : qAsConst(m_accounts)) {
        if (id == actor.id()) {
            return actor;
        }
    }

    qDebug() << "Can't find actor with id:" << id;
    return Actor<KeyPrivate>();
}

Actor<KeyPrivate> AccountController::getActor(int number) {
    //    return actorIndex->getActor(BigNumber(number));
    if (number >= 0 && !m_accounts.isEmpty() && number < m_accounts.size()) {
        return m_accounts.at(number);
    }
    qDebug() << "Can't find actor with index:" << number;
    return Actor<KeyPrivate>();
}

const Actor<KeyPrivate> &AccountController::mainActor() {
    if (m_accounts.isEmpty()) {
        qFatal("[AccountController] No main actor");
        std::exit(-1);
    }
    return m_accounts.first();
}

Actor<KeyPrivate> AccountController::getCurrentActor() {
    return getActor(this->userNum);
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
                this->m_accounts << actor;
            }
        }
    }

    if (this->m_accounts.size() > 0) {
        qDebug() << "[AccountController]" << this->m_accounts.size() << "accounts have been loaded" << id;
        node->blockchain()->getBlockZero();
        emit loadWallets(id, idList);
        node->start();
    } else {
        qDebug() << "[AccountController] There no accounts found locally";
    }
}

int AccountController::getAccountCount() const {
    return m_accounts.size();
}

int AccountController::getUserNum() const {
    return userNum;
}

void AccountController::setUserNum(int value) {
    userNum = value;
}

void AccountController::savePrivateActor(Actor<KeyPrivate> actor, QByteArray hashLogin) {
    qDebug() << "[AccountController] Attempting to save private actor" << actor.id();
    if (!m_accounts.isEmpty())
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
    userNum = 0;
    qDebug() << "[AccountController]" << m_accounts.size() << "accounts after Logout";
}

void AccountController::changeUserNum(QByteArray wallId) {
    userNum = 0;
    for (const auto &currAcc : qAsConst(this->m_accounts)) {
        // qDebug() << "ACCOUNT CONTROLLER: change userNum" << wallId;
        if (currAcc.id().toByteArray() == wallId) {
            emit updateTransactionListInModel();
            break;
        }
        ++userNum;
    }
}

void AccountController::process() {
}
