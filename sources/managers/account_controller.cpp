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

#include "managers/extrachain_node.h"

AccountController::AccountController(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
}

SeedProfile AccountController::create_profile(const std::string               &hash,
                                              ActorType                        type,
                                              std::optional<Actor<KeyPrivate>> predefine_actor) {
    if (hash.empty()) {
        eFatal("[Accounts] Create actor: hash is empty");
    }

    auto seed     = Cryptography::generate_seed();
    profile_type_ = ProfileType::New;

    Actor<KeyPrivate> system_actor;
    if (predefine_actor.has_value()) {
        system_actor = predefine_actor.value();
    } else {
        system_actor.generate_from_seed(seed, 0, type);
    }

    Actor<KeyPrivate> main_actor;
    main_actor.generate_from_seed(seed, 1, ActorType::User);

    auto profile = PrivateProfile::create(system_actor, main_actor, hash, node);
    m_profiles.push_back(profile);
    m_currentProfile = system_actor.id();
    node->actorIndex()->store_new_actor(system_actor.to_public());
    node->actorIndex()->store_new_actor(main_actor.to_public());
    insert_to_profile_set(system_actor.id());
    autologinHash.save(hash); // TODO: add arg

    SeedProfile profile_seed;
    profile_seed.set(seed);
    profile_seed.generate();
    profile_seed.save(hash);
    this->profile_seed = profile_seed;

    eLog("[Accounts] Created new profile. System: {}, main: {}", system_actor.id(), main_actor.id());

    node->start(); // TODO: remove

    node->calculateBlockCount();

    return profile_seed;
}

Actor<KeyPrivate> AccountController::createWallet(const ActorId &profileActor, const std::string &wallet_name) {
    Actor<KeyPrivate> actor;

    if (profile_type_ == ProfileType::Old) {
        actor.create(ActorType::User);
    } else {
        actor.generate_from_seed(profile_seed.seed(), profile_seed.actors().size(), ActorType::User);
    }

    auto &profile = getProfile(profileActor.is_zero() ? m_currentProfile : profileActor);
    //
    profile.add_wallet(actor, profile_type_ == ProfileType::Old);

    if (profile_type_ == ProfileType::Old) {
        profile.rename_wallet(actor.id(), wallet_name);
    }

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

void AccountController::import_old_profile(const ImportedUser &imported_profile, const std::string &hash) {
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

        if (!profile.has_value()) {
            auto try_new = SeedProfile::load(actor_id.to_string(), key_result.value());
            if (try_new.has_value()) {
                count++;
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
    auto key_result = Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return false;
    }

    auto profile = PrivateProfile::load(actor_id, hash, node, key_result.value());

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
    } else {
        auto try_new = SeedProfile::load(actor_id.to_string(), key_result.value());
        if (try_new.has_value()) {

            auto profile = PrivateProfile::create(try_new->actors()[0], try_new->actors()[1], hash, node, false);

            const auto actors = try_new->generate_other(node);
            if (!actors.empty()) {
                for (const auto &actor : actors) {
                    profile.add_wallet(actor, false);
                }
            }

            m_profiles.push_back(profile);
            m_currentProfile = try_new->actors()[0].id();
            insert_to_profile_set(try_new->actors()[0].id());
            autologinHash.save(hash); // TODO: add arg

            this->profile_seed = try_new.value();
            profile_type_      = ProfileType::New;
            node->start();
            return true;
        }
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

const std::vector<ActorId> AccountController::accounts_ids() const {
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

std::vector<std::string> AccountController::seed_mnemonic() {
    auto seed     = profile_seed.seed();
    auto mnemonic = Cryptography::create_mnemonic(seed);
    return mnemonic;
}

bool AccountController::validate_mnemonic(const std::string &phrase) {
    return Cryptography::validate_mnemonic(phrase);
}

std::string AccountController::seed_hex() {
    if (Utils::is_container_empty(profile_seed.seed())) {
        return "";
    }

    std::string hex;
    auto        hash = currentProfile().hash();
    if (hash.empty()) {
        return "";
    }

    auto encrypt_result =
        Cryptography::symmetric_encrypt_password(ByteArray(profile_seed.seed()).toBytes(), hash, true);
    if (!encrypt_result.has_value()) {
        return "";
    }

    hex = Utils::to_hex(ByteArray(encrypt_result.value()).toBytes());

    return hex;
}

bool AccountController::import_seed_phrase(const std::string &login,
                                           const std::string &password,
                                           const std::string &phrase) {
    auto seed = Cryptography::restore_seed_from_mnemonic(phrase);
    if (!seed.has_value()) {
        return false;
    }

    return import_seed(login, password, seed.value());
}

bool AccountController::import_seed_hex(const std::string &login,
                                        const std::string &password,
                                        const std::string &seed_hex) {
    auto hash            = Utils::calculate_hash(login + password);
    auto bytes           = Utils::from_hex(seed_hex);
    auto encrypted_bytes = ByteArray(bytes).toBytes();

    auto seed = Cryptography::symmetric_decrypt_password(encrypted_bytes, hash, true);
    if (!seed.has_value()) {
        return false;
    }

    return import_seed(login, password, ByteArray(seed.value()).toArray<32>());
}

bool AccountController::import_seed(const std::string &login,
                                    const std::string &password,
                                    const MasterSeed  &seed) {
    SeedProfile seed_profile;
    seed_profile.set(seed);
    seed_profile.generate();
    // profile_seed.generate_other(node);
    auto hash = Utils::calculate_hash(login + password);
    auto res  = seed_profile.save(hash);

    insert_to_profile_set(seed_profile.actors().front().id());
    return res.has_value();
}

void AccountController::dogenerate() {
    if (profile_type_ == ProfileType::Old) {
        return;
    }

    const auto actors = profile_seed.generate_other(node);

    if (actors.empty()) {
        return;
    }

    for (const auto &actor : actors) {
        getProfile(m_currentProfile).add_wallet(actor, false);
    }

    emit dogenerated();
}
