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
#include "profile/private_profile.h"

class ExtraChainNode;

/**
 * @brief The AccountController class
 * One client can have several accounts, so AccountController is storing this accounts
 * and provides access to them.
 */
class EXTRACHAIN_EXPORT AccountController {
private:
    ExtraChainNode &node;

    std::vector<PrivateProfile> m_profiles;
    ActorId m_currentUser;

public:
    AccountController(ExtraChainNode &node);

public:
    /**
     * @brief Generates a new actor and adds it into accounts list
     * @return created actor
     */
    Actor<KeyPrivate> createUser(const std::string &hash);
    Actor<KeyPrivate> createWallet(const ActorId &userActor = ActorId());
    // createService
    // createServiceProvider

    bool load(const std::string &hash);

    const Actor<KeyPrivate> &mainActor();

    PrivateProfile &getProfile(const ActorId &actorId);
    /**
     * @brief Gets current active user
     * @return actor
     */
    const PrivateProfile &currentUser() const;

    int count() const;
    void changeCurrentUser(const ActorId &actorId);

    // const std::vector<Actor<KeyPrivate>> &accounts() const;
    const std::vector<Actor<KeyPrivate>> &accounts() const; // temp
    const Actor<KeyPrivate> &currentWallet() const;         // temp
    void clear();

    static std::vector<ActorId> profilesList();
    void addToProfileList(const ActorId &actorId);
};

#endif // ACCOUNT_CONTROLLER_H
