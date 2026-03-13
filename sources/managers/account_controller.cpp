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
#include "dfs/dfs_controller.h"

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
    profiles_.push_back(profile);
    current_profile_ = system_actor.id();
    node->actor_index()->store_new_actor(system_actor.to_public());
    node->actor_index()->store_new_actor(main_actor.to_public());
    insert_to_profile_set(system_actor.id());
    autologin_hash.save(hash); // TODO: add arg

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

Actor<KeyPrivate> AccountController::create_wallet(const ActorId &profileActor, const std::string &wallet_name) {
    Actor<KeyPrivate> actor;

    if (profile_type_ == ProfileType::Old) {
        actor.create(ActorType::User);
    } else {
        actor.generate_from_seed(profile_seed.seed(), profile_seed.actors().size(), ActorType::User);
    }

    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    //
    profile.add_wallet(actor, profile_type_ == ProfileType::Old);

    if (profile_type_ == ProfileType::Old) {
        profile.rename_wallet(actor.id(), wallet_name);
    }

    node->actor_index()->store_new_actor(actor.to_public());
    return actor;
}

Actor<KeyPrivate> AccountController::create_service(const ActorId                   &profileActor,
                                                    std::optional<Actor<KeyPrivate>> predefined_actor) {
    Actor<KeyPrivate> actor;
    if (predefined_actor.has_value())
        actor = predefined_actor.value();
    else
        actor.create(ActorType::Service);
    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    profile.add_wallet(actor);
    node->actor_index()->store_new_actor(actor.to_public());
    return actor;
}

void AccountController::import_old_profile(const ImportedUser &imported_profile, const std::string &hash) {
    auto              profile = PrivateProfile::import(imported_profile, hash, node);
    Actor<KeyPrivate> actor   = profile.system();

    for (const auto &actor : profile.actors()) {
        node->actor_index()->save_actor(actor.to_public());
    }
    for (const auto &actor : profile.imports()) {
        node->actor_index()->save_actor(actor.to_public());
    }

    insert_to_profile_set(actor.id());
    eLog("[Accounts] Imported profile: {}", imported_profile);
}

bool AccountController::rename_wallet(const ActorId     &profileActor,
                                      const ActorId     &actorId,
                                      const std::string &walletName) {
    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    return profile.rename_wallet(actorId, walletName);
}

std::expected<void, LoadError> AccountController::load(const std::string &hash) {
    if (hash.empty()) {
        return std::unexpected(LoadError::EmptyHash);
    }

    auto profiles = profiles_list();

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
        auto res = this->load_profile(actor_id, hash, key_result.value());
        if (res) {
            return {};
        }
    }

    return std::unexpected(LoadError::Unknown);
}

bool AccountController::load_profile(const ActorId                &actor_id,
                                     const std::string            &hash,
                                     const std::optional<KeyPass> &key) {
    auto key_result = key.has_value() ? key.value() : Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return false;
    }

    auto profile = PrivateProfile::load(actor_id, hash, node, key_result.value());

    if (profile.loaded()) {
        const auto &actors = profile.actors();
        for (auto &actor : actors) {
            if (node->actor_index()->read_by_id(actor.id()).isEmpty()) {
                node->actor_index()->save_actor(actor.to_public());
            }

            node->dfs()->add_priority_actor(actor.id());
        }

        profiles_.push_back(profile);
        current_profile_ = profile.system().id();
        node->start();             // TODO: remove
        autologin_hash.save(hash); // TODO: add arg
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

            profiles_.push_back(profile);
            current_profile_ = try_new->actors()[0].id();
            insert_to_profile_set(try_new->actors()[0].id());
            autologin_hash.save(hash); // TODO: add arg

            this->profile_seed = try_new.value();
            profile_type_      = ProfileType::New;
            node->start();
            return true;
        }
    }

    return false;
}

std::set<ActorId> AccountController::multiple_profiles(const std::string &hash) {
    auto              profiles = profiles_list();
    std::set<ActorId> multiple_profiles;

    auto key_result = Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return {};
    }

    for (auto &actor_id : profiles) {
        auto profile = PrivateProfile::read(actor_id, hash, node, key_result.value());
        if (profile.has_value()) {
            multiple_profiles.insert(actor_id);
        } else {
            auto try_new = SeedProfile::load(actor_id.to_string(), key_result.value());
            if (try_new.has_value()) {
                multiple_profiles.insert(actor_id);
            }
        }
    }

    return multiple_profiles;
}

const Actor<KeyPrivate> &AccountController::system_actor() {
    if (profiles_.empty()) {
        eFatal("[AccountController] No system actor");
    }

    return current_profile().system();
}

PrivateProfile &AccountController::profile(const ActorId &actorId) {
    for (auto &profile : profiles_) {
        if (actorId == profile.system().id()) {
            return profile;
        }
    }

    eFatal("getProfile: Can't find actor");
    return profiles_.front();
}

const PrivateProfile &AccountController::current_profile() const {
    if (current_profile_.is_zero()) {
        eFatal("Incorrect current profile");
    }

    for (auto &profile : profiles_) {
        if (current_profile_ == profile.system().id()) {
            return profile;
        }
    }

    eFatal("Can't find actor");
    QCoreApplication::exit(-123);

    return profiles_.front();
}

int AccountController::count() const {
    return profiles_.size();
}

bool AccountController::empty() const {
    return profiles_.empty();
}

void AccountController::change_current_profile(const ActorId &actorId) {
    if (!profile(actorId).actors().empty()) {
        current_profile_ = actorId.to_string();
    }
}

const std::vector<Actor<KeyPrivate>> &AccountController::accounts() const {
    return current_profile().actors();
}

const std::vector<ActorId> AccountController::accounts_ids() const {
    std::vector<ActorId> ids;
    for (int i = 0; i < current_profile().actors().size(); i++) {
        ids.push_back(current_profile().actors()[i].id());
    }
    return ids;
}

const Actor<KeyPrivate> &AccountController::current_wallet() const {
    return current_profile().current();
}

void AccountController::clear() {
    profiles_.clear();
    current_profile_ = ActorId();
    eLog("[AccountController] Cleared");
}

std::set<ActorId> AccountController::profiles_list() {
    QString file_name = QString::fromStdString(Profiles::folder + Utils::platformDelimeter() + Profiles::profiles);
    QFile file(file_name);
    if (!file.exists())
        return {};

    if (!file.open(QFile::ReadOnly)) {
        eWarning("Failed to open file: %s. Error: %s", file_name, file.errorString());
        return {};
    }

    auto json_bytes    = file.readAll();
    auto profiles_json = QJsonDocument::fromJson(json_bytes).array();

    std::set<ActorId> profiles;

    for (auto actor_id : profiles_json) {
        profiles.insert(ActorId(actor_id.toString().toStdString()));
    }

    return profiles;
}

void AccountController::insert_to_profile_set(const ActorId &actorId) {
    auto profiles = profiles_list();
    profiles.insert(actorId);
    QJsonArray array;
    for (auto &actorId : profiles) {
        array.push_back(actorId.toQString());
    }
    auto json = QJsonDocument(array).toJson(QJsonDocument::Compact);

    QString file_name = QString::fromStdString(Profiles::folder + Utils::platformDelimeter() + Profiles::profiles);
    QFile file(file_name);
    if (!file.open(QFile::ReadOnly)) {
        eWarning("Failed to open file: %s. Error: %s", file_name, file.errorString());
        return;
    }

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
    auto        hash = current_profile().hash();
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

    static bool dogenerated = false;
    if (dogenerated) {
        return;
    }

    dogenerated       = true;
    const auto actors = profile_seed.generate_other(node);

    if (actors.empty()) {
        return;
    }

    for (const auto &actor : actors) {
        profile(current_profile_).add_wallet(actor, false);
    }

    emit this->dogenerated();
}
