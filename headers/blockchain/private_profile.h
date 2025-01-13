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

#pragma once

#include "blockchain/actor.h"

enum class PrivateProfileError {
    Unknown,
    NoActor,
    ZeroActor
};

using PrivateActors = std::vector<Actor<KeyPrivate>>;

class EXTRACHAIN_EXPORT PrivateProfile {
public:
    static PrivateProfile                 create(const Actor<KeyPrivate> &actor, const std::string &hash);
    static PrivateProfile                 load(const ActorId &actorId, const std::string &hash);
    const Actor<KeyPrivate>              &system() const;
    const Actor<KeyPrivate>              &current() const;
    const std::vector<Actor<KeyPrivate>> &actors() const;
    bool                                  change_current(const ActorId &actorId);
    void                                  add_wallet(const Actor<KeyPrivate> &actor);
    bool                                  rename_wallet(const ActorId &actorId, const std::string &walletName);

    // TODO: use std::reference_wrapper
    const std::expected<Actor<KeyPrivate>, PrivateProfileError> get_actor(const ActorId &actorId) const;
    bool                                                        loaded();
    const std::string                                          &hash() const;
    QJsonObject                                                 toJson() const;

    std::map<ActorId, std::string> wallet_names() const;

    void add_imported_actor(const Actor<KeyPrivate> &imported_actor);

private:
    PrivateProfile() = default;

    void                  save();
    void                  load();
    std::filesystem::path path();

    ActorId                        system_;
    ActorId                        current_;
    std::string                    hash_;
    PrivateActors                  actors_;
    PrivateActors                  imports_;
    std::map<ActorId, std::string> wallet_names_;
};
