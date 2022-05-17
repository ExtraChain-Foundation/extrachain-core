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

#ifndef ACCOUNT_CONTROLLER_H
#define ACCOUNT_CONTROLLER_H

#include <QDebug>

#include "datastorage/actor.h"
#include "datastorage/private_profile.h"
#include "utils/autologinhash.h"

class ExtraChainNode;

/**
 * @brief The AccountController class
 * One client can have several accounts, so AccountController is storing this accounts
 * and provides access to them.
 */
class EXTRACHAIN_EXPORT AccountController {
public:
    explicit AccountController(ExtraChainNode &node);

    /**
     * @brief Generates a new actor and adds it into accounts list
     * @return created actor
     */
    Actor<KeyPrivate> createProfile(const std::string &hash, ActorType type = ActorType::User);
    Actor<KeyPrivate> createWallet(const ActorId &profileActor = ActorId());
    // createService
    // createServiceProvider

    bool load(const std::string &hash);

    const Actor<KeyPrivate> &mainActor();

    PrivateProfile &getProfile(const ActorId &actorId);
    /**
     * @brief Gets current active profile
     * @return actor
     */
    const PrivateProfile &currentProfile() const;

    int count() const;
    void changeCurrentProfile(const ActorId &actorId);

    // const std::vector<Actor<KeyPrivate>> &accounts() const;
    const std::vector<Actor<KeyPrivate>> &accounts() const; // temp
    const Actor<KeyPrivate> &currentWallet() const;         // temp
    void clear();

    static std::vector<ActorId> profilesList();
    void addToProfileList(const ActorId &actorId);

private:
    ExtraChainNode &node;
    AutologinHash autologinHash;

    std::vector<PrivateProfile> m_profiles;
    ActorId m_currentProfile;
};

#endif // ACCOUNT_CONTROLLER_H
