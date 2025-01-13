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

#include "encryption/encryption_tools.h"
#include "utils/exc_utils.h"

PrivateProfile PrivateProfile::create(const Actor<KeyPrivate> &actor, const std::string &hash) {
    PrivateProfile user;
    user.actors_.push_back(actor);
    user.system_ = actor.id();
    user.hash_   = hash;
    user.save();
    return user;
}

PrivateProfile PrivateProfile::load(const ActorId &actorId, const std::string &hash) {
    PrivateProfile user;
    user.system_ = actorId;
    user.hash_   = hash;
    user.load();
    return user;
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
        eFatal("ExtraUser system error");
    }
    return current_actor->get();
}

const std::vector<Actor<KeyPrivate>> &PrivateProfile::actors() const {
    return actors_;
}

const std::vector<Actor<KeyPrivate>> &PrivateProfile::imports() const {
    return imports_;
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

bool PrivateProfile::rename_wallet(const ActorId &actorId, const std::string &walletName) {
    if (walletName.empty()) {
        return false;
    }

    wallet_names_[actorId] = walletName;
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

void PrivateProfile::save() {
    auto json_bytes = Json::serialize(*this);
    auto encrypted  = Cryptography::symmetric_encrypt_password(Bytes(json_bytes.begin(), json_bytes.end()), hash_);
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

void PrivateProfile::load() {
    std::ifstream file(path(), std::ios::binary);
    if (!file) {
        eFatal("Can't open file");
    }
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto json_bytes = Cryptography::symmetric_decrypt_password(Bytes(data.begin(), data.end()), hash_);
    if (!json_bytes.has_value()) {
        eFatal("Incorrect private profile load");
    }

    auto profile = Json::deserialize<PrivateProfile>(json_bytes.value());
    if (!profile.has_value()) {
        eFatal("Incorrect private profile load: incorrect json");
    }

    this->system_  = profile->system_;
    this->current_ = profile->system_;
    this->actors_  = profile->actors_;
    this->imports_ = profile->imports_;
}

std::filesystem::path PrivateProfile::path() {
    return KeyStore::folder + Utils::platformDelimeter() + system_.to_string() + KeyStore::format;
}

std::map<ActorId, std::string> PrivateProfile::wallet_names() const {
    return wallet_names_;
}

void PrivateProfile::add_imported_actor(const Actor<KeyPrivate> &imported_actor) {
    imports_.push_back(imported_actor);
}
