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

#include "chain/actor.h"
#include "chain/private_profile.h"
#include "utils/autologinhash.h"

class ExtraChainNode;

enum class LoadError {
    Unknown,
    EmptyHash,
    NoProfiles,
    NoAuthProfiles,
    Multiple
};

enum class ProfileType {
    Old,
    New
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
    SeedProfile       create_profile(const std::string               &hash,
                                     ActorType                        type,
                                     std::optional<Actor<KeyPrivate>> predefine_actor = std::nullopt);
    Actor<KeyPrivate> create_wallet(const ActorId     &profileActor = ActorId(),
                                    const std::string &wallet_name  = std::string());
    // Actor<KeyPrivate> create_dapp_master
    Actor<KeyPrivate> create_service(const ActorId                   &profileActor     = ActorId(),
                                     std::optional<Actor<KeyPrivate>> predefined_actor = std::nullopt);

    void import_old_profile(const ImportedUser &imported_profile, const std::string &hash);

    bool rename_wallet(const ActorId &profileActor, const ActorId &actorId, const std::string &walletName);

    std::expected<void, LoadError> load(const std::string &hash);
    bool load_profile(const ActorId &actor_id, const std::string &hash, const std::optional<KeyPass> &key);
    std::set<ActorId> multiple_profiles(const std::string &hash);

    // TODO: expected?
    const Actor<KeyPrivate> &system_actor();

    PrivateProfile &profile(const ActorId &actorId);
    /**
     * @brief Gets current active profile
     * @return actor
     */
    const PrivateProfile &current_profile() const;

    int  count() const;
    bool empty() const;
    void change_current_profile(const ActorId &actorId);

    // const std::vector<Actor<KeyPrivate>> &accounts() const;
    const std::vector<Actor<KeyPrivate>> &accounts() const; // temp
    const std::vector<ActorId>            accounts_ids() const;
    const Actor<KeyPrivate>              &current_wallet() const; // temp
    void                                  clear();

    static std::set<ActorId> profiles_list();
    void                     insert_to_profile_set(const ActorId &actorId);

private:
    ExtraChainNode *node;
    AutologinHash   autologin_hash; // for debug builds

    std::vector<PrivateProfile> profiles_;
    ActorId                     current_profile_;
    ProfileType                 profile_type_ = ProfileType::Old;

public:
    SeedProfile profile_seed;

    ProfileType profile_type() {
        return profile_type_;
    }

    std::vector<std::string> seed_mnemonic();
    bool                     validate_mnemonic(const std::string &phrase);
    std::string              seed_hex();

    bool import_seed_phrase(const std::string &login, const std::string &password, const std::string &phrase);
    bool import_seed_hex(const std::string &login, const std::string &password, const std::string &seed_hex);
    bool import_seed(const std::string &login, const std::string &password, const MasterSeed &seed);

    void dogenerate();

signals:
    void dogenerated();
};
