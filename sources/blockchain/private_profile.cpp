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

#include <QJsonObject>

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

QJsonObject PrivateProfile::toJson() const {
    QJsonObject json;
    json["main"] = system_.toQString();

    QJsonArray actors;
    for (const auto &actor : actors_) {
        actors.append(actor.toJsonArray());
    }
    json["actors"] = actors;

    QJsonArray imports;
    for (const auto &actor : imports_) {
        actors.append(actor.toJsonArray());
    }
    json["imports"] = imports;

    QJsonObject walletNames;
    for (const auto &[actor, name] : this->wallet_names_) {
        walletNames[actor.toQString()] = QString::fromStdString(name);
    }
    json["walletNames"] = walletNames;

    eLog("[PrivateProfile] JSON: {}", QJsonDocument(json).toJson(QJsonDocument::Compact));
    return json;
}

void PrivateProfile::save() {
    auto json_bytes = QJsonDocument(toJson()).toJson(QJsonDocument::Compact).toStdString();
    auto encrypted  = Cryptography::symmetric_encrypt_password(Bytes(json_bytes.begin(), json_bytes.end()), hash_);
    if (!encrypted.has_value()) {
        eFatal("Incorrect private profile save");
    }

    auto data = QByteArray(reinterpret_cast<const char *>(encrypted->data()), encrypted->size());
    // eLog("Save data: {}", data);
    QFile file(path().string().c_str());
    file.open(QFile::WriteOnly);
    if (file.write(data) == 0)
        eFatal("Can't write");
    file.close();
}

void PrivateProfile::load() {
    QFile file(path().string().c_str());
    file.open(QFile::ReadOnly);
    auto data       = file.readAll().toStdString();
    auto json_bytes = Cryptography::symmetric_decrypt_password(Bytes(data.begin(), data.end()), hash_);
    if (!json_bytes.has_value()) {
        eFatal("Incorrect private profile load");
    }
    auto json_bytes_qt = QByteArray(reinterpret_cast<const char *>(json_bytes->data()), json_bytes->size());

    auto json              = QJsonDocument::fromJson(json_bytes_qt).object();
    system_                = json["main"].toString().toStdString();
    const auto actors      = json["actors"].toArray();
    const auto imports     = json["actors"].toArray();
    const auto walletNames = json["walletNames"].toObject();

    for (const auto &actor : actors) {
        auto json = QJsonDocument(actor.toArray()).toJson(QJsonDocument::Compact);
        auto a    = Actor<KeyPrivate>::fromJson(json);
        actors_.push_back(a);
    }

    for (const auto &actor : imports) {
        auto json = QJsonDocument(actor.toArray()).toJson(QJsonDocument::Compact);
        auto a    = Actor<KeyPrivate>::fromJson(json);
        imports_.push_back(a);
    }

    for (auto it = walletNames.begin(); it != walletNames.end(); ++it) {
        auto actor_id = it.key().toStdString();
        auto name     = it.value().toString().toStdString();
        this->wallet_names_.insert({ ActorId(actor_id), name });
    }
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
