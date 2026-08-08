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

#include <algorithm>

#include "chain/actor_id.h"
#include "dfs/dfs_utils.h"

#include <vector>

class QNetworkAccessManager;
class QTimer;

namespace Chat {
struct Chat;
}

struct ThothDeviceRecord {
    std::string   os;
    std::string   token;
    std::string   custom;         // serialized ThothCustom, opaque here
    std::string   name;           // human-readable device name
    std::string   app;            // app version of the writing device ("0.1.1.0")
    std::uint64_t updated_at = 0; // unix ms of the last write of this record
};
BOOST_DESCRIBE_STRUCT(ThothDeviceRecord, (), (os, token, custom, name, app, updated_at))

struct ThothChatDevices {
    ActorId     owner;
    std::string file_id;
    std::map<std::string, ThothDeviceRecord> devices; // deviceId -> record
};
BOOST_DESCRIBE_STRUCT(ThothChatDevices, (), (owner, file_id, devices))

struct ThothRegistry {
    std::map<std::string, ThothChatDevices> chats;   // "owner:file_id" -> data
    // Deliberately signed-out devices: deviceId -> unix ms of the revocation.
    // Distinguishes remote logout from a sync clobber (which self-heal repairs).
    std::map<std::string, std::uint64_t>    revoked;
};
BOOST_DESCRIBE_STRUCT(ThothRegistry, (), (chats, revoked))

struct ThothInfo {
    std::string       id;
    std::string       os;
    std::string       token;
    std::set<ActorId> ignored;

    bool operator==(const ThothInfo& other) const {
        return os == other.os && token == other.token && ignored == other.ignored;
    }

    bool operator<(const ThothInfo& other) const {
        if (os != other.os) {
            return os < other.os;
        }

        if (token != other.token) {
            return token < other.token;
        }

        return std::lexicographical_compare(ignored.begin(),
                                            ignored.end(),
                                            other.ignored.begin(),
                                            other.ignored.end());
    }
};
BOOST_DESCRIBE_STRUCT(ThothInfo, (), (id, os, token, ignored))

struct ThothCustom {
    std::set<ActorId> ignored;
};
BOOST_DESCRIBE_STRUCT(ThothCustom, (), (ignored));

struct ThothServiceMessage {
    std::string device_token;
    std::string title;
    std::string body;
};
BOOST_DESCRIBE_STRUCT(ThothServiceMessage, (), (device_token, title, body));

// Aggregated per-device view for the "My devices" UI.
struct ThothDeviceInfo {
    std::string   device_id;
    std::string   name;
    std::string   os;
    std::string   app;
    std::uint64_t updated_at = 0;
    bool          is_current = false;
    std::size_t   chats      = 0;
};

class ExtraChainNode;

enum class ThothType {
    ChatMessage
};

struct ThothPendingRecord {
    ActorId     owner_id;
    std::string file_id;
    std::string custom;
};

class ThothManager : public QObject {
    Q_OBJECT

public:
    ThothManager(ExtraChainNode* node, QObject* parent = nullptr);

    // Creates the current Thoth key-value registry. The legacy "Thoth" vector is not used.
    bool create_thoth_dictionary();
    bool read_all(bool is_my);
    void dfs_vector_add_check(const ActorId& owner_id, const std::string& file_id, const DbRow& row);
    void network_thoth_record(const ActorId& owner_id, const std::string& file_id, const DbRow& row);

    // Registers the token for every chat in the list; called by ChatManager::read_chats().
    void reconcile_tokens_for_chats(const std::vector<Chat::Chat>& chats);

    void start();
    void stop();

    // for users
    bool add_thoth_record(const ActorId& owner_id, const std::string& file_id, const std::string& custom);
    // bool remove_thoth_record(const ActorId& owner_id, const std::string& file_id)

    bool send_to_service(const ThothInfo& info, const std::string& username);

    // Platform-neutral alias: stores the device push token (APNS on iOS, FCM on Android).
    void        set_device_token(const std::string& token);
    void        set_ios_token(const std::string& token);

    // Stable platform device id injected by the app (ANDROID_ID / Keychain). Falls back
    // to a random id persisted in the data dir (test/dev fallback only).
    void        set_device_id(const std::string& id);

    // Human-readable name written into this device's records (defaults from QSysInfo).
    void        set_device_name(const std::string& name);

    // Current device id (ensures it is loaded/created first).
    const std::string& device_id();

    // "My devices": aggregate this account's own registry across all chats.
    std::vector<ThothDeviceInfo> my_devices();

    // Erase one device id from every chat in this account's registry (read->merge->write).
    bool remove_device(const std::string& device_id);

    std::string read_username(const ActorId& actor_id);

private:
    ExtraChainNode* node;

    bool          enabled_ = false;
    std::uint16_t port_    = 5425;

    ActorId                                      owner_id_;
    std::string                                  file_id_;
    std::map<Dfs::FileLink, std::set<ThothInfo>> infos_;
    std::string                                  usernames_file_id_;
    std::vector<ThothPendingRecord>              pending_records_;

    // #ifdef Q_OS_IOS
    std::string ios_token_;
    // #endif

    // Reconcile anti-spam guard: redo only when the token or the chat count changed.
    std::string reconciled_token_;
    std::size_t reconciled_chats_count_ = 0;
    // Force a registry rewrite on start / after token or device-id change (self-healing).
    bool        force_registry_write_ = true;

    // One dictionary key per account: first 20 hex of a local "Thoth" actor derived from seed.
    std::string registry_key_;
    std::string registry_key();

    void persist_device_tokens();
    void load_persisted_device_tokens();
    void load_or_create_device_id();
    // Records written by the last reconcile; used to detect and heal sync clobbering.
    std::vector<ThothPendingRecord> last_reconciled_records_;
    void verify_self_registration();
    void reconcile_after_token_change();
    std::uint64_t last_self_heal_ms_ = 0;
    // True once the registry marked this device revoked; gates all writes.
    bool revoked_ = false;
    // Returns true (and emits deviceRevoked once) when reg revokes this device.
    bool check_revocation(const ThothRegistry& reg);
    // Revocations authored by THIS device (persisted): each device re-asserts
    // its own deletions if a stale sync copy resurrects the victim.
    std::map<std::string, std::uint64_t> my_revocations_;
    bool my_revocations_loaded_ = false;
    // Periodic pull of the registry (dirs refresh of the network actor) so a
    // revocation reaches an idle device even without a broadcast (e.g. relay
    // dedup by node identifier). Stopped permanently once revoked.
    QTimer* revocation_watchdog_ = nullptr;
    std::atomic_bool enabled_watchdog_ { false };
    void load_my_revocations();
    void persist_my_revocations();
    // Applies my_revocations_ to reg (erase records, union tombstones);
    // returns true when reg was modified.
    bool enforce_my_revocations(ThothRegistry& reg);

    std::string device_name_;
    std::string effective_device_name();
    static std::string detect_os();

signals:
    void sendSuccess(const QString& response);
    void sendFailed(const QString& error);
    // This device found itself in the registry's revoked list: it was signed
    // out remotely. The app should log out (wipe local data).
    void deviceRevoked();

private:
    QNetworkAccessManager* m_networkManager;

    void enqueue_thoth_record(const ActorId& owner_id, const std::string& file_id, const std::string& custom);
    void flush_pending_records();
    void apply_thoth_row(const DbRow& row);
    void remove_thoth_info(const std::string& id);
    std::string device_id_;

};
