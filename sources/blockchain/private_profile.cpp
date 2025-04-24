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

#include "blockchain/private_profile.h"

#include "managers/extrachain_node.h"
#include "encryption/encryption_tools.h"
#include "utils/exc_utils.h"

PrivateProfile PrivateProfile::create(const Actor<KeyPrivate> &system_actor,
                                      const Actor<KeyPrivate> &main_actor,
                                      const std::string       &hash,
                                      ExtraChainNode          *node) {
    PrivateProfile user;
    user.actors_.push_back(system_actor);
    user.actors_.push_back(main_actor);

    user.system_        = system_actor.id();
    user.main_          = main_actor.id();
    user.hash_          = hash;
    user.creation_date_ = Utils::current_date_ms();
    user.modified_date_ = user.creation_date_;
    user.node           = node;
    user.save();
    return user;
}

std::expected<PrivateProfile, PrivateProfileReadError> PrivateProfile::read(const ActorId                &actor_id,
                                                                            const std::string            &hash,
                                                                            ExtraChainNode               *node,
                                                                            const std::optional<KeyPass> &key) {
    PrivateProfile user;
    user.system_ = actor_id;
    user.hash_   = hash;
    user.node    = node;
    return user.read(key);
}

PrivateProfile PrivateProfile::load(const ActorId                &actor_id,
                                    const std::string            &hash,
                                    ExtraChainNode               *node,
                                    const std::optional<KeyPass> &key) {
    PrivateProfile user;
    user.system_ = actor_id;
    user.hash_   = hash;
    user.node    = node;
    user.load(key);
    return user;
}

PrivateProfile PrivateProfile::import(const ImportedUser &imported_user,
                                      const std::string  &hash,
                                      ExtraChainNode     *node) {
    PrivateProfile private_profile;
    private_profile.hash_         = hash;
    private_profile.actors_       = imported_user.actors;
    private_profile.imports_      = imported_user.imports;
    private_profile.wallet_names_ = imported_user.wallet_names;

    if (imported_user.creation_date == 0) {
        // Version compatibility: 0.15.0
        private_profile.creation_date_ = Utils::current_date_ms();
        private_profile.modified_date_ = private_profile.creation_date_;
    } else {
        private_profile.creation_date_ = imported_user.creation_date;
        private_profile.modified_date_ = imported_user.modified_date;
    }

    private_profile.system_  = imported_user.system;
    private_profile.main_    = imported_user.main;
    private_profile.current_ = imported_user.system;

    private_profile.save();
    return private_profile;
}

const Actor<KeyPrivate> &PrivateProfile::system() const {
    if (system_.is_zero()) {
        eFatal("ExtraUser system error");
    }

    auto system_actor = get_actor(system_);
    if (!system_actor.has_value()) {
        eFatal("ExtraUser system error");
    }
    return system_actor->get();
}

const Actor<KeyPrivate> &PrivateProfile::current() const {
    if (current_.is_zero())
        return system();

    auto current_actor = get_actor(system_);
    if (!current_actor.has_value()) {
        eFatal("ExtraUser current error");
    }
    return current_actor->get();
}

const std::vector<Actor<KeyPrivate>> &PrivateProfile::actors() const {
    return actors_;
}

const std::vector<Actor<KeyPrivate>> &PrivateProfile::imports() const {
    return imports_;
}

std::expected<std::reference_wrapper<const Actor<KeyPrivate>>, PrivateProfileError> PrivateProfile::main() const {
    return get_actor(main_);
}

bool PrivateProfile::change_current(const ActorId &actorId) {
    auto changed_actor = get_actor(actorId);
    if (!changed_actor.has_value()) {
        return false;
    }

    current_ = actorId;
    return true;
}

void PrivateProfile::add_wallet(const Actor<KeyPrivate> &actor) {
    actors_.push_back(actor);
    save();
}

bool PrivateProfile::rename_wallet(const ActorId &actor_id, const std::string &wallet_name) {
    if (wallet_name.empty()) {
        return false;
    }

    // TODO: check unicode

    // if (wallet_name.size() > 50) {
    //     return false;
    // }

    bool is_exists = node->actorIndex()->exists(actor_id);
    if (!is_exists) {
        return false;
    }

    this->wallet_names_[actor_id] = wallet_name;
    // eLog("_____> {} {} {}", fmt::ptr(this), fmt::ptr(&wallet_names_), wallet_names_);

    save();
    return true;
}

std::expected<std::reference_wrapper<const Actor<KeyPrivate>>, PrivateProfileError> PrivateProfile::get_actor(
    const ActorId &actorId) const {
    if (actorId.is_zero()) {
        return std::unexpected(PrivateProfileError::ZeroActor);
    }

    for (const auto &actor : std::as_const(actors_)) {
        if (actorId == actor.id()) {
            return std::ref(actor);
        }
    }

    return std::unexpected(PrivateProfileError::NoActor);
}

bool PrivateProfile::loaded() {
    return !system_.is_zero() && !actors_.empty();
}

const std::string &PrivateProfile::hash() const {
    return hash_;
}

void PrivateProfile::save(uint64_t modified_date) {
    if (modified_date == 0) {
        modified_date = Utils::current_date_ms();
    }
    this->modified_date_ = modified_date;

    auto json_bytes = Json::serialize(*this);
    // eLog("_____ {}", json_bytes);
    auto encrypted = Cryptography::symmetric_encrypt_password(Bytes(json_bytes.begin(), json_bytes.end()), hash_);
    if (!encrypted.has_value()) {
        eFatal("Incorrect private profile save");
    }

    std::ofstream file(path(), std::ios::binary);
    if (!file) {
        eFatal("Can't open file for writing");
    }

    if (!file.write(reinterpret_cast<const char *>(encrypted->data()), encrypted->size())) {
        eFatal("Can't write");
    }
    file.close();
}

std::expected<PrivateProfile, PrivateProfileReadError> PrivateProfile::read(const std::optional<KeyPass> &key) {
    std::ifstream file(path(), std::ios::binary);
    if (!file) {
        return std::unexpected(PrivateProfileReadError::File);
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto json_bytes = key.has_value() ? Cryptography::symmetric_decrypt(ByteArray(data).toBytes(), key.value())
                                      : Cryptography::symmetric_decrypt_password(ByteArray(data).toBytes(), hash_);
    if (!json_bytes.has_value()) {
        // eWarning("Incorrect private profile load");
        return std::unexpected(PrivateProfileReadError::Decrypt);
    }

    auto profile = Json::deserialize<PrivateProfile>(json_bytes.value());
    if (!profile.has_value()) {
        // eWarning("Incorrect private profile load: incorrect json");
        return std::unexpected(PrivateProfileReadError::Json);
    }

    return profile.value();
}

void PrivateProfile::load(const std::optional<KeyPass> &key) {
    auto profile = this->read(key);
    if (!profile.has_value()) {
        return;
    }

    this->system_       = profile->system_;
    this->current_      = profile->system_;
    this->main_         = profile->main_;
    this->actors_       = profile->actors_;
    this->imports_      = profile->imports_;
    this->wallet_names_ = profile->wallet_names_;

    if (profile->creation_date_ == 0) {
        // Version compatibility: 0.15.0
        auto file_creation_time = Utils::read_file_creation_time_ms(path());

        this->creation_date_ =
            file_creation_time.has_value() ? file_creation_time.value() : Utils::current_date_ms();
        this->modified_date_ = this->creation_date_;
        this->save();
    } else {
        this->creation_date_ = profile->creation_date_;
        this->modified_date_ = profile->modified_date_;
    }

    if (profile->main_.is_zero()) {
        // Version compatibility: 0.17.0
        Actor<KeyPrivate> main_actor;
        main_actor.create(ActorType::User);
        this->actors_.insert(this->actors_.begin() + 1, main_actor);
        this->main_ = main_actor.id();
        node->actorIndex()->store_new_actor(main_actor.to_public());
        this->save();
    }
}

std::filesystem::path PrivateProfile::path() {
    return Profiles::folder + Utils::platformDelimeter() + system_.to_string() + Profiles::format;
}

std::unordered_map<ActorId, std::string> PrivateProfile::wallet_names() const {
    return wallet_names_;
}

std::expected<std::string, ImportError> PrivateProfile::export_actor(const ActorId &actor_id) {
    auto actor = get_actor(actor_id);
    if (!actor.has_value()) {
        return std::unexpected(ImportError::NoActor);
    }

    return Json::serialize(actor);
}

void PrivateProfile::add_imported_actor(const Actor<KeyPrivate> &imported_actor) {
    imports_.push_back(imported_actor);
}
