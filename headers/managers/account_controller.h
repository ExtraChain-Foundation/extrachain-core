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
#include "blockchain/private_profile.h"
#include "utils/autologinhash.h"

class ExtraChainNode;

enum class LoadError {
    Unknown,
    EmptyHash,
    NoProfiles,
    NoAuthProfiles,
    Multiple
};

/**
 * @brief The AccountController class
 * One client can have several accounts, so AccountController is storing this accounts
 * and provides access to them.
 */
class EXTRACHAIN_EXPORT AccountController : public QObject {
    Q_OBJECT

public:
    explicit AccountController(ExtraChainNode *node);

    /**
     * @brief Generates a new actor and adds it into accounts list
     * @return created actor
     */
    Actor<KeyPrivate> createProfile(const std::string               &hash,
                                    ActorType                        type            = ActorType::User,
                                    std::optional<Actor<KeyPrivate>> predefine_actor = std::nullopt);
    Actor<KeyPrivate> createWallet(const ActorId     &profileActor = ActorId(),
                                   const std::string &walletName   = std::string());
    // createDAppMaster
    Actor<KeyPrivate> createService(const ActorId                   &profileActor     = ActorId(),
                                    std::optional<Actor<KeyPrivate>> predefined_actor = std::nullopt);

    void import_profile(const ImportedUser &imported_profile, const std::string &hash);

    bool rename_wallet(const ActorId &profileActor, const ActorId &actorId, const std::string &walletName);

    std::expected<void, LoadError> load(const std::string &hash);
    bool                           load_profile(const ActorId &actor_id, const std::string &hash);
    std::set<ActorId>              multiple_profiles(const std::string &hash);

    // TODO: expected?
    const Actor<KeyPrivate> &system_actor();

    PrivateProfile &getProfile(const ActorId &actorId);
    /**
     * @brief Gets current active profile
     * @return actor
     */
    const PrivateProfile &currentProfile() const;

    int  count() const;
    bool empty() const;
    void changeCurrentProfile(const ActorId &actorId);

    // const std::vector<Actor<KeyPrivate>> &accounts() const;
    const std::vector<Actor<KeyPrivate>> &accounts() const; // temp
    const std::vector<ActorId>            accountsIds() const;
    const Actor<KeyPrivate>              &currentWallet() const; // temp
    void                                  clear();

    static std::set<ActorId> profilesList();
    void                     insert_to_profile_set(const ActorId &actorId);

private:
    ExtraChainNode *node;
    AutologinHash   autologinHash;

    std::vector<PrivateProfile> m_profiles;
    ActorId                     m_currentProfile;
};
