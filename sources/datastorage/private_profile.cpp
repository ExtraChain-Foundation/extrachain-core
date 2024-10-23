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

#include "datastorage/private_profile.h"

#include <QJsonObject>

#include "enc/enc_tools.h"
#include "utils/exc_utils.h"

PrivateProfile PrivateProfile::create(const Actor<KeyPrivate> &actor, const std::string &hash) {
    PrivateProfile user;
    user.m_actors.push_back(std::make_shared<Actor<KeyPrivate>>(actor));
    user.m_main = actor.id();
    user.m_hash = hash;
    user.save();
    return user;
}

PrivateProfile PrivateProfile::load(const ActorId &actorId, const std::string &hash) {
    PrivateProfile user;
    user.m_main = actorId;
    user.m_hash = hash;
    user.load();
    return user;
}

const std::shared_ptr<Actor<KeyPrivate>> PrivateProfile::main() const {
    if (m_main.isZero())
        qFatal("ExtraUser main error");
    return getActor(m_main);
}

const std::shared_ptr<Actor<KeyPrivate>> PrivateProfile::current() const {
    if (m_current.isZero())
        return main();
    return getActor(m_current);
}

const std::vector<std::shared_ptr<Actor<KeyPrivate>>> &PrivateProfile::actors() const {
    return m_actors;
}

bool PrivateProfile::changeCurrent(const ActorId &actorId) {
    if (getActor(actorId)->empty()) {
        qFatal("Can't find actor");
        std::exit(-123);
    }
    m_current = actorId;
    return true;
}

void PrivateProfile::addWallet(const Actor<KeyPrivate> &actor) {
    m_actors.push_back(std::make_shared<Actor<KeyPrivate>>(actor));
    save();
}

bool PrivateProfile::renameWallet(const ActorId &actorId, const std::string &walletName) {
    if (walletName.empty()) {
        return false;
    }

    walletNames[actorId] = walletName;
    save();
    return true;
}

const std::shared_ptr<Actor<KeyPrivate>> PrivateProfile::getActor(const ActorId &actorId) const {
    if (actorId == ActorId()) {
        qFatal("Can't get zero actor");
    }

    for (const auto &actor : std::as_const(m_actors)) {
        if (actorId == actor->id()) {
            return actor;
        }
    }

    qWarning("Can't find actor");
    // std::exit(-123);
    return nullptr;
}

bool PrivateProfile::loaded() {
    return !m_main.isZero() && !m_actors.empty();
}

const std::string &PrivateProfile::hash() const {
    return m_hash;
}

QJsonObject PrivateProfile::toJson() const {
    QJsonObject json;
    json["main"] = m_main.toString();

    QJsonArray actors;
    for (const auto &actor : m_actors) {
        actors.append(actor->toJsonArray());
    }
    json["actors"] = actors;

    QJsonObject walletNames;
    for (const auto &[actor, name] : this->walletNames) {
        walletNames[actor.toString()] = QString::fromStdString(name);
    }
    json["walletNames"] = walletNames;

    qDebug() << "[PrivateProfile] JSON:" << json;
    return json;
}

void PrivateProfile::save() {
    auto jsonBytes = QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
    auto data = QByteArray::fromStdString(SecretKey::encryptWithPassword(jsonBytes.toStdString(), m_hash));
    // qDebug() << "Save data:" << data;
    QFile file(path().string().c_str());
    file.open(QFile::WriteOnly);
    if (file.write(data) == 0)
        qFatal("Can't write");
    file.close();
}

void PrivateProfile::load() {
    QFile file(path().string().c_str());
    file.open(QFile::ReadOnly);
    auto data = file.readAll().toStdString();
    auto jsonBytes = QByteArray::fromStdString(SecretKey::decryptWithPassword(data, m_hash));

    auto json = QJsonDocument::fromJson(jsonBytes).object();
    m_main = json["main"].toString().toStdString();
    const auto actors = json["actors"].toArray();
    const auto walletNames = json["walletNames"].toObject();

    for (const auto &actor : actors) {
        auto json = QJsonDocument(actor.toArray()).toJson(QJsonDocument::Compact);
        auto a = Actor<KeyPrivate>::fromJson(json);
        m_actors.push_back(std::make_shared<Actor<KeyPrivate>>(a));
    }

    for (auto it = walletNames.begin(); it != walletNames.end(); ++it) {
        auto actorId = it.key().toStdString();
        auto name = it.value().toString().toStdString();
        this->walletNames.insert({ ActorId(actorId), name });
    }
}

std::filesystem::path PrivateProfile::path() {
    return KeyStore::folder + Utils::platformDelimeter() + m_main.toStdString() + KeyStore::format;
}

std::map<ActorId, std::string> PrivateProfile::getWalletNames() const
{
    return walletNames;
}
