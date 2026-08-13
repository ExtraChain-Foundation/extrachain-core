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

#include "managers/account_controller.h"

#include <boost/json.hpp>
#include <filesystem>

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "utils/file_io.h"

namespace {
    void log_actor_save_failure(const std::expected<void, ActorSaveError> &result, const ActorId &actor_id) {
        if (!result.has_value() && result.error() != ActorSaveError::AlreadyExists) {
            eWarning("[Accounts] Cannot save actor {}: error {}", actor_id, static_cast<int>(result.error()));
        }
    }
} // namespace

AccountController::AccountController(ExtraChain::Core::ExtraChainNode *node)
    : node(node) {
}

SeedProfile AccountController::create_profile(const std::string               &hash,
                                              ActorType                        type,
                                              std::optional<Actor<KeyPrivate>> predefine_actor) {
    if (hash.empty()) {
        eFatal("[Accounts] Create actor: hash is empty");
    }

    auto seed     = Cryptography::generate_seed();
    profile_type_ = ProfileType::New;

    Actor<KeyPrivate> system_actor;
    if (predefine_actor.has_value()) {
        system_actor = predefine_actor.value();
    } else {
        system_actor.generate_from_seed(seed, 0, type);
    }

    Actor<KeyPrivate> main_actor;
    main_actor.generate_from_seed(seed, 1, ActorType::User);

    auto profile = PrivateProfile::create(system_actor, main_actor, hash, node);
    profiles_.push_back(profile);
    current_profile_ = system_actor.id();
    log_actor_save_failure(node->actor_index()->store_new_actor(system_actor.to_public()), system_actor.id());
    log_actor_save_failure(node->actor_index()->store_new_actor(main_actor.to_public()), main_actor.id());
    insert_to_profile_set(system_actor.id());
    autologin_hash.save(hash); // TODO: add arg

    SeedProfile profile_seed;
    profile_seed.set(seed);
    profile_seed.generate();
    if (!profile_seed.save(hash).has_value()) {
        eCritical("[Accounts] Cannot save seed profile {}", system_actor.id());
    }
    this->profile_seed = profile_seed;

    // chat_main is in seed_profile.actors()[2]; add to profile.actors_ for DFS lookup.
    if (profile_seed.actors().size() > 2) {
        const auto &chat_main = profile_seed.actors()[2];
        this->profile(current_profile_).add_wallet(chat_main, false);
        log_actor_save_failure(node->actor_index()->store_new_actor(chat_main.to_public()), chat_main.id());
        eLog("[Accounts] chat_main registered: {}", chat_main.id());
    }

    eLog("[Accounts] Created new profile. System: {}, main: {}", system_actor.id(), main_actor.id());

    node->start(); // TODO: remove

    node->calculateBlockCount();

    return profile_seed;
}

Actor<KeyPrivate> AccountController::create_wallet(const ActorId &profileActor, const std::string &wallet_name) {
    Actor<KeyPrivate> actor;

    if (profile_type_ == ProfileType::Old) {
        actor.create(ActorType::User);
    } else {
        actor.generate_from_seed(profile_seed.seed(), profile_seed.actors().size(), ActorType::User);
    }

    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    //
    profile.add_wallet(actor, profile_type_ == ProfileType::Old);

    if (profile_type_ == ProfileType::Old) {
        profile.rename_wallet(actor.id(), wallet_name);
    }

    log_actor_save_failure(node->actor_index()->store_new_actor(actor.to_public()), actor.id());
    return actor;
}

Actor<KeyPrivate> AccountController::create_service(const ActorId                   &profileActor,
                                                    std::optional<Actor<KeyPrivate>> predefined_actor) {
    Actor<KeyPrivate> actor;
    if (predefined_actor.has_value())
        actor = predefined_actor.value();
    else
        actor.create(ActorType::Service);
    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    profile.add_wallet(actor);
    log_actor_save_failure(node->actor_index()->store_new_actor(actor.to_public()), actor.id());
    return actor;
}

Actor<KeyPrivate> AccountController::create_actor(const ActorId &profileActor, int seed_index, ActorType type) {
    Actor<KeyPrivate> actor;
    if (profile_type_ == ProfileType::Old) {
        return actor;
    }

    actor.generate_from_seed(profile_seed.seed(), seed_index, type);

    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    profile.add_wallet(actor, false);
    log_actor_save_failure(node->actor_index()->store_new_actor(actor.to_public()), actor.id());
    return actor;
}

Actor<KeyPrivate> AccountController::create_actor(const ActorId     &profileActor,
                                                  const std::string &seed_label,
                                                  ActorType          type) {
    Actor<KeyPrivate> actor;
    if (profile_type_ == ProfileType::Old) {
        return actor;
    }

    actor.generate_from_seed(profile_seed.seed(), seed_label, type);

    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    profile.add_wallet(actor, false);

    if (!node->actor_index()->exists(actor.id())) {
        log_actor_save_failure(node->actor_index()->store_new_actor(actor.to_public()), actor.id());
    }
    return actor;
}

Actor<KeyPrivate> AccountController::restore_actor(const ActorId     &profileActor,
                                                   const std::string &seed_label,
                                                   ActorType          type) {
    Actor<KeyPrivate> actor;
    if (profile_type_ == ProfileType::Old) {
        return actor;
    }

    actor.generate_from_seed(profile_seed.seed(), seed_label, type);

    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    if (!profile.get_actor(actor.id()).has_value()) {
        profile.add_wallet(actor, false);
    }
    return actor;
}

Actor<KeyPrivate> AccountController::derive_local_actor(const std::string &seed_label, ActorType type) {
    Actor<KeyPrivate> actor;
    if (profile_type_ == ProfileType::Old) {
        return actor;
    }

    actor.generate_from_seed(profile_seed.seed(), seed_label, type);
    return actor;
}

std::expected<std::reference_wrapper<const Actor<KeyPrivate>>, ChatActorError> AccountController::chat_actor() {
    if (current_profile_.is_zero() || profiles_.empty()) {
        return std::unexpected(ChatActorError::NoProfile);
    }

    // Chat identity is strictly chat_main: never fall back to main, it must
    // stay hidden from chat peers. Old profiles have no seed, so no chat.
    if (profile_type_ == ProfileType::Old) {
        return std::unexpected(ChatActorError::NoSeed);
    }

    if (profile_seed.actors().size() <= 2) {
        return std::unexpected(ChatActorError::DerivationFailed);
    }

    auto chat_id = profile_seed.actors()[2].id();
    auto stored  = this->profile(current_profile_).get_actor(chat_id);
    if (!stored.has_value()) {
        return std::unexpected(ChatActorError::DerivationFailed);
    }
    return stored.value();
}

void AccountController::import_old_profile(const ImportedUser &imported_profile, const std::string &hash) {
    auto              profile = PrivateProfile::import(imported_profile, hash, node);
    Actor<KeyPrivate> actor   = profile.system();

    for (const auto &actor : profile.actors()) {
        log_actor_save_failure(node->actor_index()->save_actor(actor.to_public()), actor.id());
    }
    for (const auto &actor : profile.imports()) {
        log_actor_save_failure(node->actor_index()->save_actor(actor.to_public()), actor.id());
    }

    insert_to_profile_set(actor.id());
    eLog("[Accounts] Imported profile: {}", imported_profile);
}

bool AccountController::rename_wallet(const ActorId     &profileActor,
                                      const ActorId     &actorId,
                                      const std::string &walletName) {
    auto &profile = this->profile(profileActor.is_zero() ? current_profile_ : profileActor);
    return profile.rename_wallet(actorId, walletName);
}

std::expected<void, LoadError> AccountController::load(const std::string &hash) {
    if (hash.empty()) {
        return std::unexpected(LoadError::EmptyHash);
    }

    auto profiles = profiles_list();

    if (profiles.empty()) {
        return std::unexpected(LoadError::NoProfiles);
    }

    auto key_result = Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return {};
    }

    int count = 0;
    for (auto &actor_id : profiles) {
        auto profile = PrivateProfile::read(actor_id, hash, node, key_result.value());
        if (profile.has_value()) {
            count++;
            if (count > 1) {
                break;
            }
        }

        if (!profile.has_value()) {
            auto try_new = SeedProfile::load(actor_id.to_string(), key_result.value());
            if (try_new.has_value()) {
                count++;
            }
        }
    }

    if (count > 1) {
        eLog("[Accounts] Multiple profiles found for this login and password combination");
        return std::unexpected(LoadError::Multiple);
    }

    if (count == 0) {
        return std::unexpected(LoadError::NoAuthProfiles);
    }

    for (auto &actor_id : profiles) {
        auto res = this->load_profile(actor_id, hash, key_result.value());
        if (res) {
            return {};
        }
    }

    return std::unexpected(LoadError::Unknown);
}

bool AccountController::load_profile(const ActorId                &actor_id,
                                     const std::string            &hash,
                                     const std::optional<KeyPass> &key) {
    auto key_result = key.has_value() ? key.value() : Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return false;
    }

    auto profile = PrivateProfile::load(actor_id, hash, node, key_result.value());

    if (profile.loaded()) {
        const auto &actors = profile.actors();
        for (auto &actor : actors) {
            if (node->actor_index()->read_by_id(actor.id()).empty()) {
                log_actor_save_failure(node->actor_index()->save_actor(actor.to_public()), actor.id());
            }

            node->dfs()->add_priority_actor(actor.id());
        }

        profiles_.push_back(profile);
        current_profile_ = profile.system().id();
        node->start();             // TODO: remove
        autologin_hash.save(hash); // TODO: add arg
        return true;
    } else {
        auto try_new = SeedProfile::load(actor_id.to_string(), key_result.value());
        if (try_new.has_value()) {

            auto profile = PrivateProfile::create(try_new->actors()[0], try_new->actors()[1], hash, node, false);

            // chat_main lives at seed_profile.actors()[2]; needs to be in profile.actors_ for DFS.
            if (try_new->actors().size() > 2) {
                const auto &chat_main = try_new->actors()[2];
                profile.add_wallet(chat_main, false);
                if (!node->actor_index()->exists(chat_main.id())) {
                    log_actor_save_failure(node->actor_index()->store_new_actor(chat_main.to_public()),
                                           chat_main.id());
                }
                eLog("[Accounts] chat_main registered (load): {}", chat_main.id());
            }

            const auto actors = try_new->generate_other(node);
            if (!actors.empty()) {
                for (const auto &actor : actors) {
                    profile.add_wallet(actor, false);
                }
            }

            profiles_.push_back(profile);
            current_profile_ = try_new->actors()[0].id();
            insert_to_profile_set(try_new->actors()[0].id());
            autologin_hash.save(hash); // TODO: add arg

            this->profile_seed = try_new.value();
            profile_type_      = ProfileType::New;
            node->start();
            return true;
        }
    }

    return false;
}

std::set<ActorId> AccountController::multiple_profiles(const std::string &hash) {
    auto              profiles = profiles_list();
    std::set<ActorId> multiple_profiles;

    auto key_result = Cryptography::key_from_password(hash);
    if (!key_result.has_value()) {
        return {};
    }

    for (auto &actor_id : profiles) {
        auto profile = PrivateProfile::read(actor_id, hash, node, key_result.value());
        if (profile.has_value()) {
            multiple_profiles.insert(actor_id);
        } else {
            auto try_new = SeedProfile::load(actor_id.to_string(), key_result.value());
            if (try_new.has_value()) {
                multiple_profiles.insert(actor_id);
            }
        }
    }

    return multiple_profiles;
}

const Actor<KeyPrivate> &AccountController::system_actor() {
    if (profiles_.empty()) {
        eFatal("[AccountController] No system actor");
    }

    return current_profile().system();
}

PrivateProfile &AccountController::profile(const ActorId &actorId) {
    for (auto &profile : profiles_) {
        if (actorId == profile.system().id()) {
            return profile;
        }
    }

    eFatal("getProfile: Can't find actor");
    return profiles_.front();
}

const PrivateProfile &AccountController::current_profile() const {
    if (current_profile_.is_zero()) {
        eFatal("Incorrect current profile");
    }

    for (auto &profile : profiles_) {
        if (current_profile_ == profile.system().id()) {
            return profile;
        }
    }

    eFatal("Can't find actor");
    throw std::logic_error("Current profile is not loaded");
}

int AccountController::count() const {
    return profiles_.size();
}

bool AccountController::empty() const {
    return profiles_.empty();
}

void AccountController::change_current_profile(const ActorId &actorId) {
    if (!profile(actorId).actors().empty()) {
        current_profile_ = actorId.to_string();
    }
}

const std::vector<Actor<KeyPrivate>> &AccountController::accounts() const {
    return current_profile().actors();
}

const std::vector<ActorId> AccountController::accounts_ids() const {
    const auto          &actors = current_profile().actors();
    std::vector<ActorId> ids;
    ids.reserve(actors.size());
    for (const auto &actor : actors) {
        ids.push_back(actor.id());
    }
    return ids;
}

const Actor<KeyPrivate> &AccountController::current_wallet() const {
    return current_profile().current();
}

void AccountController::clear() {
    profiles_.clear();
    current_profile_ = ActorId();
    eLog("[AccountController] Cleared");
}

std::set<ActorId> AccountController::profiles_list() {
    const auto file_name = std::filesystem::path(Profiles::folder) / Profiles::profiles;
    const auto content   = FileIo::read_all(file_name);
    if (!content.has_value()) {
        return {};
    }
    std::set<ActorId>         profiles;
    boost::system::error_code error;
    const auto                parsed = boost::json::parse(*content, error);
    if (error || !parsed.is_array()) {
        eWarning("Failed to parse profile index: {}", error.message());
        return {};
    }
    for (const auto &actor_id : parsed.as_array()) {
        if (actor_id.is_string()) {
            profiles.emplace(std::string(actor_id.as_string()));
        }
    }
    return profiles;
}

void AccountController::insert_to_profile_set(const ActorId &actorId) {
    auto profiles = profiles_list();
    profiles.insert(actorId);
    boost::json::array array;
    array.reserve(profiles.size());
    for (const auto &profile_id : profiles) {
        array.emplace_back(profile_id.to_string());
    }
    const auto      folder = std::filesystem::path(Profiles::folder);
    std::error_code directory_error;
    std::filesystem::create_directories(folder, directory_error);
    if (directory_error) {
        eWarning("Failed to create profile folder: {}", directory_error.message());
        return;
    }
    if (!FileIo::write_atomic(folder / Profiles::profiles, boost::json::serialize(array)).has_value()) {
        eWarning("Failed to write profile index");
    }
}

std::vector<std::string> AccountController::seed_mnemonic() {
    auto seed = profile_seed.seed();
    if (Utils::is_container_empty(seed)) {
        eWarning("[Accounts] Can't export seed mnemonic: profile seed is empty");
        return {};
    }

    auto mnemonic = Cryptography::create_mnemonic(seed);
    if (mnemonic.size() != 24) {
        eWarning("[Accounts] Can't export seed mnemonic: unexpected word count {}", mnemonic.size());
        return {};
    }

    return mnemonic;
}

bool AccountController::validate_mnemonic(const std::string &phrase) {
    return Cryptography::validate_mnemonic(phrase);
}

std::string AccountController::seed_hex() {
    if (Utils::is_container_empty(profile_seed.seed())) {
        return "";
    }

    std::string hex;
    auto        hash = current_profile().hash();
    if (hash.empty()) {
        return "";
    }

    auto encrypt_result =
        Cryptography::symmetric_encrypt_password(ByteArray(profile_seed.seed()).toBytes(), hash, true);
    if (!encrypt_result.has_value()) {
        return "";
    }

    hex = Utils::to_hex(ByteArray(encrypt_result.value()).toBytes());

    return hex;
}

bool AccountController::import_seed_phrase(const std::string &login,
                                           const std::string &password,
                                           const std::string &phrase) {
    auto seed = Cryptography::restore_seed_from_mnemonic(phrase);
    if (!seed.has_value()) {
        return false;
    }

    return import_seed(login, password, seed.value());
}

bool AccountController::import_seed_hex(const std::string &login,
                                        const std::string &password,
                                        const std::string &seed_hex) {
    auto hash            = Utils::calculate_hash(login + password);
    auto bytes           = Utils::from_hex(seed_hex);
    auto encrypted_bytes = ByteArray(bytes).toBytes();

    auto seed = Cryptography::symmetric_decrypt_password(encrypted_bytes, hash, true);
    if (!seed.has_value()) {
        return false;
    }

    return import_seed(login, password, ByteArray(seed.value()).toArray<32>());
}

bool AccountController::import_seed(const std::string &login,
                                    const std::string &password,
                                    const MasterSeed  &seed) {
    SeedProfile seed_profile;
    seed_profile.set(seed);
    seed_profile.generate();
    // profile_seed.generate_other(node);
    auto hash = Utils::calculate_hash(login + password);
    auto res  = seed_profile.save(hash);

    insert_to_profile_set(seed_profile.actors().front().id());
    return res.has_value();
}

void AccountController::dogenerate() {
    if (profile_type_ == ProfileType::Old) {
        return;
    }

    static bool dogenerated = false;
    if (dogenerated) {
        return;
    }

    dogenerated       = true;
    const auto actors = profile_seed.generate_other(node);

    if (actors.empty()) {
        return;
    }

    for (const auto &actor : actors) {
        profile(current_profile_).add_wallet(actor, false);
    }

    generated_event_.publish();
}

ExtraChain::Core::Event<> &AccountController::generated_event() noexcept {
    return generated_event_;
}
