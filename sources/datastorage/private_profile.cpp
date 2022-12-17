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
    user.m_actors.push_back(actor);
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

const Actor<KeyPrivate> &PrivateProfile::main() const {
    if (m_main.isEmpty())
        qFatal("ExtraUser main error");
    return getActor(m_main);
}

const Actor<KeyPrivate> &PrivateProfile::current() const {
    if (m_current.isEmpty())
        return main();
    return getActor(m_current);
}

const std::vector<Actor<KeyPrivate>> &PrivateProfile::actors() const {
    return m_actors;
}

bool PrivateProfile::changeCurrent(const ActorId &actorId) {
    if (getActor(actorId).empty()) {
        qFatal("Can't find actor");
        std::exit(-123);
    }
    m_current = actorId;
    return true;
}

void PrivateProfile::addWalet(const Actor<KeyPrivate> &actor) {
    m_actors.push_back(actor);
    save();
}

const Actor<KeyPrivate> &PrivateProfile::getActor(const ActorId &actorId) const {
    for (const auto &actor : qAsConst(m_actors)) {
        if (actorId == actor.id()) {
            return actor;
        }
    }

    qFatal("Can't find actor");
    std::exit(-123);
    return m_actors.front();
}

bool PrivateProfile::loaded() {
    return !m_main.isEmpty() && !m_actors.empty();
}

const std::string &PrivateProfile::hash() const {
    return m_hash;
}

QJsonObject PrivateProfile::toJson() const {
    QJsonObject json;
    json["main"] = m_main.toString();

    QJsonArray actors;
    for (const auto &actor : m_actors) {
        actors.append(actor.toJsonArray());
    }
    json["actors"] = actors;
    return json;
}

void PrivateProfile::save() {
    auto jsonBytes = QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
    auto data = QByteArray::fromStdString(SecretKey::encryptWithPassword(jsonBytes.toStdString(), m_hash));

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
    // decrypt with m_hash
    auto json = QJsonDocument::fromJson(jsonBytes).object();
    m_main = json["main"].toString().toStdString();
    const auto actors = json["actors"].toArray();
    for (const auto &actor : actors) {
        auto json = QJsonDocument(actor.toArray()).toJson(QJsonDocument::Compact);
        auto a = Actor<KeyPrivate>::fromJson(json);
        m_actors.push_back(a);
    }
}

std::filesystem::path PrivateProfile::path() {
    return KeyStore::folder + Utils::platformDelimeter() + m_main.toStdString() + KeyStore::format;
}
