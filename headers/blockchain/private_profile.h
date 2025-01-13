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

enum class ImportError {
    NoNetworkId,
    EmptyProfile,
    CryptoError,
    NoActor
};

struct ImportedUser {
    ActorId                        network;
    std::string                    version;
    uint64_t                       date;
    ActorId                        system;
    std::vector<Actor<KeyPrivate>> actors;
    std::vector<Actor<KeyPrivate>> imports;
    std::map<ActorId, std::string> wallet_names;
};
BOOST_DESCRIBE_STRUCT(ImportedUser, (), (network, date, system, actors, imports, wallet_names))

class EXTRACHAIN_EXPORT PrivateProfile {
public:
    PrivateProfile() = default; // only for json
    static PrivateProfile create(const Actor<KeyPrivate> &actor, const std::string &hash);
    static PrivateProfile load(const ActorId &actor_id, const std::string &hash);
    static PrivateProfile import(const ImportedUser &imported_user, const std::string &hash);

    const Actor<KeyPrivate>              &system() const;
    const Actor<KeyPrivate>              &current() const;
    const std::vector<Actor<KeyPrivate>> &actors() const;
    const std::vector<Actor<KeyPrivate>> &imports() const;
    bool                                  change_current(const ActorId &actorId);
    void                                  add_wallet(const Actor<KeyPrivate> &actor);
    bool                                  rename_wallet(const ActorId &actorId, const std::string &walletName);

    std::expected<std::reference_wrapper<const Actor<KeyPrivate>>, PrivateProfileError> get_actor(
        const ActorId &actorId) const;
    bool               loaded();
    const std::string &hash() const;

    std::map<ActorId, std::string> wallet_names() const;

    std::expected<std::string, ImportError> export_actor(const ActorId &actor_id);
    void                                    add_imported_actor(const Actor<KeyPrivate> &imported_actor);

private:
    void                  save();
    void                  load();
    std::filesystem::path path();

    ActorId                        system_;
    ActorId                        current_;
    std::string                    hash_;
    std::vector<Actor<KeyPrivate>> actors_;
    std::vector<Actor<KeyPrivate>> imports_;
    std::map<ActorId, std::string> wallet_names_;

    BOOST_DESCRIBE_CLASS(PrivateProfile, (), (), (), (system_, actors_, imports_, wallet_names_))
};
