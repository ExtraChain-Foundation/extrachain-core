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

class QNetworkAccessManager;

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
    std::string       os;
    std::string       token;
    std::set<ActorId> ignored;

    auto operator<=>(const ThothInfo&) const = default;
};
BOOST_DESCRIBE_STRUCT(ThothInfo, (), (os, token, ignored))

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

    void start();
    void stop();

    // for users
    bool add_thoth_record(const ActorId& owner_id, const std::string& file_id, const std::string& custom);
    // bool remove_thoth_record(const ActorId& owner_id, const std::string& file_id)

    bool send_to_service(const ThothInfo& info, const std::string& username);

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

    // #ifdef Q_OS_IOS
    std::string ios_token_;
    // #endif

signals:
    void sendSuccess(const QString& response);
    void sendFailed(const QString& error);

private:
    QNetworkAccessManager* m_networkManager;
};
