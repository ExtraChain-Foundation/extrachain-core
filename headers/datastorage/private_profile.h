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

#ifndef PRIVATE_PROFILE_H
#define PRIVATE_PROFILE_H

#include "datastorage/actor.h"

class EXTRACHAIN_EXPORT PrivateProfile {
public:
    static PrivateProfile create(const Actor<KeyPrivate> &actor, const std::string &hash);
    static PrivateProfile load(const ActorId &actorId, const std::string &hash);
    const Actor<KeyPrivate> &main() const;
    const Actor<KeyPrivate> &current() const;
    const std::vector<Actor<KeyPrivate>> &actors() const;
    bool changeCurrent(const ActorId &actorId);
    void addWalet(const Actor<KeyPrivate> &actor);
    const Actor<KeyPrivate> &getActor(const ActorId &actorId) const;
    bool loaded();
    const std::string &hash() const;
    QJsonObject toJson() const;

private:
    PrivateProfile() = default;

    void save();
    void load();
    std::filesystem::path path();

    ActorId m_main;
    ActorId m_current;
    std::string m_hash;
    std::vector<Actor<KeyPrivate>> m_actors;
};

#endif // PRIVATE_PROFILE_H
