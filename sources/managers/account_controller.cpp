/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include "blockchain/blockchain.h"
#include "blockchain/actor_index.h"
#include "managers/transaction_manager.h"

AccountController::AccountController(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
}

Actor<KeyPrivate> AccountController::createProfile(const std::string               &hash,
                                                   ActorType                        type,
                                                   std::optional<Actor<KeyPrivate>> predefine_actor) {
    if (hash.empty())
        eFatal("[Accounts] Create actor: hash is empty");

    Actor<KeyPrivate> system_actor;
    if (predefine_actor.has_value())
        system_actor = predefine_actor.value();
    else
        system_actor.create(type);

    Actor<KeyPrivate> main_actor;
    main_actor.create(ActorType::User);

    auto profile = PrivateProfile::create(system_actor, main_actor, hash, node);
    m_profiles.push_back(profile);
    m_currentProfile = system_actor.id();
    node->actorIndex()->store_new_actor(system_actor.to_public());
    node->actorIndex()->store_new_actor(main_actor.to_public());
    insert_to_profile_set(system_actor.id());
    autologinHash.save(hash); // TODO: add arg

    eLog("[Accounts] Created new profile. System: {}, main: {}", system_actor.id(), main_actor.id());

    node->start(); // TODO: remove

    node->calculateBlockCount();
    //    if (!(type == ActorType::createDAppMaster)) // TODO: remove
    //        node.blockchain()->getBlockZero();
    return system_actor;
}

Actor<KeyPrivate> AccountController::createWallet(const ActorId &profileActor, const std::string &walletName) {
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    auto &profile = getProfile(profileActor.is_zero() ? m_currentProfile : profileActor);
    profile.add_wallet(actor);
    profile.rename_wallet(actor.id(), walletName);
    node->actorIndex()->store_new_actor(actor.to_public());
    return actor;
}

Actor<KeyPrivate> AccountController::createService(const ActorId                   &profileActor,
                                                   std::optional<Actor<KeyPrivate>> predefined_actor) {
    Actor<KeyPrivate> actor;
    if (predefined_actor.has_value())
        actor = predefined_actor.value();
    else
        actor.create(ActorType::Service);
    auto &profile = getProfile(profileActor.is_zero() ? m_currentProfile : profileActor);
    profile.add_wallet(actor);
    node->actorIndex()->store_new_actor(actor.to_public());
    return actor;
}

void AccountController::import_profile(const ImportedUser &imported_profile, const std::string &hash) {
    auto              profile = PrivateProfile::import(imported_profile, hash, node);
    Actor<KeyPrivate> actor   = profile.system();

    for (const auto &actor : profile.actors()) {
        node->actorIndex()->save_actor(actor.to_public());
    }
    for (const auto &actor : profile.imports()) {
        node->actorIndex()->save_actor(actor.to_public());
    }

    insert_to_profile_set(actor.id());
    eLog("[Accounts] Imported profile: {}", imported_profile);
}

bool AccountController::rename_wallet(const ActorId     &profileActor,
                                     const ActorId     &actorId,
                                     const std::string &walletName) {
    auto &profile = getProfile(profileActor.is_zero() ? m_currentProfile : profileActor);
    return profile.rename_wallet(actorId, walletName);
}

std::expected<void, LoadError> AccountController::load(const std::string &hash) {
    if (hash.empty()) {
        return std::unexpected(LoadError::EmptyHash);
    }

    auto profiles = profilesList();

    if (profiles.empty()) {
        return std::unexpected(LoadError::NoProfiles);
    }

    auto key_result = Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return {};
    }

    int count = 0;
    for (auto &actor_id : profiles) {
        auto profile = PrivateProfile::read(actor_id, hash, node, key_result.value());
        if (profile.has_value()) {
            count++;
            if (count > 1) {
                break;
            }
        }
    }

    if (count > 1) {
        eLog("[Accounts] Multiple profiles found for this login and password combination");
        return std::unexpected(LoadError::Multiple);
    }

    if (count == 0) {
        return std::unexpected(LoadError::NoAuthProfiles);
    }

    for (auto &actor_id : profiles) {
        auto res = load_profile(actor_id, hash);
        if (res) {
            return {};
        }
    }

    return std::unexpected(LoadError::Unknown);
}

bool AccountController::load_profile(const ActorId &actor_id, const std::string &hash) {
    auto profile = PrivateProfile::load(actor_id, hash, node);

    if (profile.loaded()) {
        const auto &actors = profile.actors();
        for (auto &actor : actors) {
            if (node->actorIndex()->getById(actor.id()).isEmpty()) {
                node->actorIndex()->save_actor(actor.to_public());
            }
        }

        m_profiles.push_back(profile);
        m_currentProfile = profile.system().id();
        node->start();            // TODO: remove
        autologinHash.save(hash); // TODO: add arg
        return true;
    }

    return false;
}

std::set<ActorId> AccountController::multiple_profiles(const std::string &hash) {
    auto              profiles = profilesList();
    std::set<ActorId> multiple_profiles;

    auto key_result = Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return {};
    }

    for (auto &actor_id : profiles) {
        auto profile = PrivateProfile::read(actor_id, hash, node, key_result.value());
        if (profile.has_value()) {
            multiple_profiles.insert(actor_id);
        }
    }

    return multiple_profiles;
}

const Actor<KeyPrivate> &AccountController::system_actor() {
    if (m_profiles.empty()) {
        eFatal("[AccountController] No system actor");
    }
    return currentProfile().system();
}

PrivateProfile &AccountController::getProfile(const ActorId &actorId) {
    for (auto &profile : m_profiles) {
        if (actorId == profile.system().id()) {
            return profile;
        }
    }

    eFatal("getProfile: Can't find actor");
    return m_profiles.front();
}

const PrivateProfile &AccountController::currentProfile() const {
    if (m_currentProfile.is_zero())
        eFatal("Incorrect current profile");

    for (auto &profile : m_profiles) {
        if (m_currentProfile == profile.system().id()) {
            return profile;
        }
    }

    eFatal("Can't find actor");
    QCoreApplication::exit(-123);
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
        m_currentProfile = actorId.to_string();
    }
}

const std::vector<Actor<KeyPrivate>> &AccountController::accounts() const {
    return currentProfile().actors();
}

const std::vector<ActorId> AccountController::accountsIds() const {
    std::vector<ActorId> ids;
    for (int i = 0; i < currentProfile().actors().size(); i++) {
        ids.push_back(currentProfile().actors()[i].id());
    }
    return ids;
}

const Actor<KeyPrivate> &AccountController::currentWallet() const {
    return currentProfile().current();
}

void AccountController::clear() {
    m_profiles.clear();
    m_currentProfile = ActorId();
    eLog("[AccountController] Cleared");
}

std::set<ActorId> AccountController::profilesList() {
    QFile file(QString::fromStdString(Profiles::folder + Utils::platformDelimeter() + Profiles::profiles));
    if (!file.exists())
        return {};

    file.open(QFile::ReadOnly);
    auto jsonBytes    = file.readAll();
    auto profilesJson = QJsonDocument::fromJson(jsonBytes).array();

    std::set<ActorId> profiles;

    for (auto actorId : profilesJson) {
        profiles.insert(ActorId(actorId.toString().toStdString()));
    }

    return profiles;
}

void AccountController::insert_to_profile_set(const ActorId &actorId) {
    auto profiles = profilesList();
    profiles.insert(actorId);
    QJsonArray array;
    for (auto &actorId : profiles) {
        array.push_back(actorId.toQString());
    }
    auto json = QJsonDocument(array).toJson(QJsonDocument::Compact);

    QFile file(QString::fromStdString(Profiles::folder + Utils::platformDelimeter() + Profiles::profiles));
    file.open(QFile::WriteOnly);
    file.write(json);
    file.close();
}
