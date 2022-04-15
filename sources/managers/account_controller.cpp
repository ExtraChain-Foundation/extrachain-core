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
#include "profile/private_profile.h"

AccountController::AccountController(ExtraChainNode &node)
    : node(node) {
}

Actor<KeyPrivate> AccountController::createUser(const std::string &hash) {
    if (hash.empty())
        qFatal("[Accounts] Create actor: hash is empty");

    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    auto profile = PrivateProfile::createUser(actor, hash);
    m_profiles.push_back(profile);
    m_currentUser = actor.id();
    node.actorIndex()->addActor(actor.convertToPublic());
    addToProfileList(actor.id());
    autologinHash.save(hash); // TODO: add arg

    qDebug() << "[Accounts] Created new user" << actor.id();

    node.start(); // TODO: remove

    if (!m_profiles.empty()) // TODO: remove
        node.blockchain()->getBlockZero();

    return actor;
}

Actor<KeyPrivate> AccountController::createWallet(const ActorId &userActor) {
    auto hash = ""; // node->privateProfile()->hash();
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    auto &profile = getProfile(userActor.isEmpty() ? m_currentUser : userActor);
    profile.addWalet(actor);
    node.actorIndex()->addActor(actor.convertToPublic());

    return actor;
}

bool AccountController::load(const std::string &hash) {
    auto profiles = profilesList();

    for (auto &actorId : profiles) {
        auto profile = PrivateProfile::loadUser(actorId, hash);
        if (profile.loaded()) {
            const auto &actors = profile.actors();
            for (auto &actor : actors) {
                if (node.actorIndex()->getById(actor.id()).isEmpty()) {
                    node.actorIndex()->addActor(actor.convertToPublic());
                }
            }

            m_profiles.push_back(profile);
            m_currentUser = profile.main().id();
            node.start(); // TODO: remove
            autologinHash.save(hash); // TODO: add arg
            return true;
        }
    }

    return false;
}

const Actor<KeyPrivate> &AccountController::mainActor() {
    if (m_profiles.empty()) {
        qFatal("[AccountController] No main actor");
        std::exit(-1);
    }
    return currentUser().main();
}

PrivateProfile &AccountController::getProfile(const ActorId &actorId) {
    for (auto &profile : m_profiles) {
        if (actorId == profile.main().id()) {
            return profile;
        }
    }

    qFatal("Can't find actor");
    std::exit(-123);
    return m_profiles.front();
}

const PrivateProfile &AccountController::currentUser() const {
    if (m_currentUser.isEmpty())
        qFatal("Incorrect current user");

    for (auto &profile : m_profiles) {
        if (m_currentUser == profile.main().id()) {
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

void AccountController::changeCurrentUser(const ActorId &actorId) {
    if (!getProfile(actorId).actors().empty()) {
        m_currentUser = actorId.toStdString();
    }
}

const std::vector<Actor<KeyPrivate>> &AccountController::accounts() const {
    return currentUser().actors();
}

const Actor<KeyPrivate> &AccountController::currentWallet() const {
    return currentUser().current();
}

void AccountController::clear() {
    m_profiles.clear();
    m_currentUser = ActorId();
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
