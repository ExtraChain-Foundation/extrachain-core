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

#include "chain/actor_id.h"
#include "dfs/dfs_utils.h"

#include <vector>

class QNetworkAccessManager;

namespace Chat {
struct Chat;
}

struct ThothData {
    std::string   id;
    std::uint64_t timestamp = 0;
    ActorId       actor;
    ActorId       owner;
    std::string   file_id;
    std::string   os;
    std::string   token;
    std::string   custom;
};
BOOST_DESCRIBE_STRUCT(ThothData, (), (id, timestamp, actor, owner, file_id, os, token, custom))

struct ThothInfo {
    std::string       id;
    std::string       os;
    std::string       token;
    std::set<ActorId> ignored;

    auto operator<=>(const ThothInfo&) const = default;
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

class ExtraChainNode;

enum class ThothType {
    ChatMessage
};

struct ThothPendingRecord {
    ActorId     owner_id;
    std::string file_id;
    std::string custom;
};

enum class ThothAddResult {
    Success,
    Retry,
    Failed
};

class ThothManager : public QObject {
    Q_OBJECT

public:
    ThothManager(ExtraChainNode* node, QObject* parent = nullptr);

    // for network
    bool create_thoth_template();

    // for apps
    bool create_thoth_vector();
    bool read_all(bool is_my);
    void dfs_vector_add_check(const ActorId& owner_id, const std::string& file_id, const DbRow& row);
    void network_thoth_record(const ActorId& owner_id, const std::string& file_id, const DbRow& row);

    // Registers the current token for every chat in the given (already-ready) list.
    // Called by ChatManager::read_chats(). No-op in the base (0.25/thoth).
    void reconcile_tokens_for_chats(const std::vector<Chat::Chat>& chats);

    void start();
    void stop();

    // for users
    bool add_thoth_record(const ActorId& owner_id, const std::string& file_id, const std::string& custom);
    // bool remove_thoth_record(const ActorId& owner_id, const std::string& file_id)

    bool send_to_service(const ThothInfo& info, const std::string& username);

    // Platform-neutral alias: stores the device push token (APNS on iOS, FCM on Android).
    // The platform is distinguished by the "os" field in ThothData, not by this setter.
    void        set_device_token(const std::string& token);
    void        set_ios_token(const std::string& token);
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

    // Guard against reconcile spam: chatsLoaded() can fire repeatedly. We only redo the
    // per-chat token registration when the token or the chat count actually changed.
    std::string reconciled_token_;
    std::size_t reconciled_chats_count_ = 0;

signals:
    void sendSuccess(const QString& response);
    void sendFailed(const QString& error);

private:
    QNetworkAccessManager* m_networkManager;

    void enqueue_thoth_record(const ActorId& owner_id, const std::string& file_id, const std::string& custom);
    ThothAddResult try_add_thoth_record(const ActorId&     owner_id,
                                        const std::string& file_id,
                                        const std::string& custom,
                                        bool               check_existing = true);
    void flush_pending_records();
    void apply_thoth_row(const DbRow& row);
    void remove_thoth_info(const std::string& id);
    bool thoth_record_exists(const ActorId& owner_id, const std::string& file_id, const std::string& custom);

    // Removes this device's own Thoth rows (actor == system_actor) that carry the given token,
    // across every chat. Used on token refresh so the stale token stops receiving pushes.
    void remove_own_records_with_token(const std::string& token);

    // Stale token whose rows couldn't be removed yet (Thoth vector not ready);
    // retried on the next flush_pending_records().
    std::string pending_remove_token_;

    // Last token persisted to <dataDir>/.thoth_device_token, so a token change across
    // process restarts still removes the previous token's rows.
    void        persist_device_token(const std::string& token);
    std::string load_persisted_device_token();
};
