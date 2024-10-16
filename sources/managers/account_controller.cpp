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

#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/transaction_manager.h"

AccountController::AccountController(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
}

Actor<KeyPrivate> AccountController::createProfile(const std::string &hash, ActorType type) {
    if (hash.empty())
        qFatal("[Accounts] Create actor: hash is empty");

    Actor<KeyPrivate> actor;
    actor.create(type);
    auto profile = PrivateProfile::create(actor, hash);
    m_profiles.push_back(profile);
    m_currentProfile = actor.id();
    node->actorIndex()->addActor(actor.convertToPublic());
    addToProfileList(actor.id());
    autologinHash.save(hash); // TODO: add arg

    qDebug() << "[Accounts] Created new profile:" << actor.id();

    node->start(); // TODO: remove
    node->transactionManager()->runMakeAndProveBlockTimers();

    node->calculateBlockCount();
    //    if (!(type == ActorType::createDAppMaster)) // TODO: remove
    //        node.blockchain()->getBlockZero();
    return actor;
}

Actor<KeyPrivate>
AccountController::createWallet(const ActorId &profileActor, const std::string &walletName) {
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    auto &profile = getProfile(profileActor.isZero() ? m_currentProfile : profileActor);
    profile.addWallet(actor);
    profile.renameWallet(actor.id(), walletName);
    node->actorIndex()->addActor(actor.convertToPublic());
    return actor;
}

Actor<KeyPrivate> AccountController::createService(const ActorId &profileActor) {
    Actor<KeyPrivate> actor;
    actor.create(ActorType::Service);
    auto &profile = getProfile(profileActor.isZero() ? m_currentProfile : profileActor);
    profile.addWallet(actor);
    node->actorIndex()->addActor(actor.convertToPublic());
    return actor;
}

void AccountController::renameWallet(
    const ActorId &profileActor,
    const ActorId &actorId,
    const std::string &walletName) {
    auto &profile = getProfile(profileActor.isZero() ? m_currentProfile : profileActor);
    profile.renameWallet(actorId, walletName);
}

bool AccountController::load(const std::string &hash) {
    auto profiles = profilesList();

    for (auto &actorId : profiles) {
        auto profile = PrivateProfile::load(actorId, hash);
        if (profile.loaded()) {
            const auto &actors = profile.actors();
            for (auto &actor : actors) {
                if (node->actorIndex()->getById(actor->id()).isEmpty()) {
                    node->actorIndex()->addActor(actor->convertToPublic());
                }
            }

            m_profiles.push_back(profile);
            m_currentProfile = profile.main()->id();
            node->start(); // TODO: remove
            if (this->empty())
                node->transactionManager()->runMakeAndProveBlockTimers();
            autologinHash.save(hash); // TODO: add arg
            return true;
        }
    }

    return false;
}

const std::shared_ptr<Actor<KeyPrivate>> AccountController::mainActor() {
    if (m_profiles.empty()) {
        qFatal("[AccountController] No main actor");
        std::exit(-1);
    }
    return currentProfile().main();
}

PrivateProfile &AccountController::getProfile(const ActorId &actorId) {
    for (auto &profile : m_profiles) {
        if (actorId == profile.main()->id()) {
            return profile;
        }
    }

    qFatal("Can't find actor");
    std::exit(-123);
    return m_profiles.front();
}

const PrivateProfile &AccountController::currentProfile() const {
    if (m_currentProfile.isZero())
        qFatal("Incorrect current profile");

    for (auto &profile : m_profiles) {
        if (m_currentProfile == profile.main()->id()) {
            return profile;
        }
    }

    qFatal("Can't find actor");
    std::exit(-123);
    return m_profiles.front();
}

int AccountController::count() const {
    return m_profiles.size();
}

bool AccountController::empty() const {
    return m_profiles.empty();
}

void AccountController::changeCurrentProfile(const ActorId &actorId) {
    if (!getProfile(actorId).actors().empty()) {
        m_currentProfile = actorId.toStdString();
    }
}

const std::vector<std::shared_ptr<Actor<KeyPrivate>>> &AccountController::accounts() const {
    return currentProfile().actors();
}

const std::vector<ActorId> AccountController::accountsIds() const {
    std::vector<ActorId> ids;
    for (int i = 0; i < currentProfile().actors().size(); i++) {
        ids.push_back(currentProfile().actors()[i]->id());
    }
    return ids;
}

const std::shared_ptr<Actor<KeyPrivate>> AccountController::currentWallet() const {
    return currentProfile().current();
}

void AccountController::clear() {
    m_profiles.clear();
    m_currentProfile = ActorId();
    qDebug() << "[AccountController] Cleared";
}

std::vector<ActorId> AccountController::profilesList() {
    QFile file(QString::fromStdString(KeyStore::folder + Utils::platformDelimeter() + KeyStore::profiles));
    if (!file.exists())
        return {};

    file.open(QFile::ReadOnly);
    auto jsonBytes = file.readAll();
    auto profilesJson = QJsonDocument::fromJson(jsonBytes).array();

    std::vector<ActorId> profiles;

    for (auto actorId : profilesJson) {
        profiles.push_back(actorId.toString().toStdString());
    }

    return profiles;
}

void AccountController::addToProfileList(const ActorId &actorId) {
    auto profiles = profilesList();
    profiles.push_back(actorId.toStdString());
    QJsonArray array;
    for (auto &actorId : profiles) {
        array.push_back(actorId.toString());
    }
    auto json = QJsonDocument(array).toJson(QJsonDocument::Compact);

    QFile file(QString::fromStdString(KeyStore::folder + Utils::platformDelimeter() + KeyStore::profiles));
    file.open(QFile::WriteOnly);
    file.write(json);
    file.close();
}
