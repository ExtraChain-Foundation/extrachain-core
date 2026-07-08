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

#include "chat/chat_manager.h"

#include "chat/chat.h"
#include "dfs/dfs_controller.h"
#include "encryption/encryption_tools.h"
#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "chain/actor_index.h"
#include "managers/thoth_manager.h"

#include <set>

namespace {
bool is_valid_chat_link(const Chat::Chat& chat) {
    return !chat.owner_id.is_zero() && !chat.file_id.empty();
}

bool same_chat_link(const Chat::Chat& lhs, const Chat::Chat& rhs) {
    return lhs.owner_id == rhs.owner_id && lhs.file_id == rhs.file_id;
}
} // namespace

ChatManager::ChatManager(ExtraChainNode* node)
    : node(node) {
    QObject::connect(node->dfs(), &DfsController::downloaded, [this](ActorId owner_id, Dfs::DirRow dir_row) {
        if (!this->activated_) {
            return;
        }

        auto chat_actor_id = this->current_chat_actor_id();
        if (owner_id != chat_actor_id) {
            return;
        }

        auto my_chats = this->read_my_chats_row();
        if (my_chats.has_value()) {
            if (my_chats->file_id == dir_row.file_id) {
                emit this->node->chatsLoaded();
                return;
            }
        }

        this->parse_invite(owner_id, dir_row);
    });

    QObject::connect(node->dfs(), &DfsController::downloaded, [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
        for (const auto& chat : std::as_const(chats_)) {
            if ((chat.owner_id != owner_id && chat.chat.peer_id != owner_id)
                || chat.file_id != dir_row.file_id) {
                continue;
            }

            emit node->chatUpdated(chat);
        }
    });

    QObject::connect(node->dfs(),
                     &DfsController::vectorRowAdded,
                     [this](ActorId owner_id, Dfs::DirRow dir_row, DbRow row) {
                         if (row["status"] != "1") {
                             return;
                         }

                         for (auto& chat : chats_) {
                             if ((chat.owner_id == owner_id || chat.chat.peer_id == owner_id)
                                 && chat.file_id == dir_row.file_id) {
                                 bool encryption = chat.chat_key.has_value();
                                 if (chat.chat.chat_type.has_value()
                                     && chat.chat.chat_type == Chat::ChatType::Channel) {
                                     encryption = false;
                                 }

                                 Dfs::DataSecurityData security;
                                 if (encryption) {
                                     security = Dfs::DataSecurityKey { .key = chat.chat_key.value() };
                                 }

                                 auto message_row = this->node->dfs()->read_vector_row(owner_id,
                                                                                        dir_row.file_id,
                                                                                        row["id"],
                                                                                        security);
                                 if (!message_row.has_value()) {
                                     return;
                                 }

                                 message_row->erase("sign");
                                 message_row->erase("status");
                                 auto message = Utils::from_dbrow<Chat::Message>(message_row.value());
                                 if (!message.has_value()) {
                                     return;
                                 }

                                 // Capture peer's per_chat from Join message if not yet known.
                                 if (message->message.type == Chat::MessageType::Join
                                     && !chat.peer_per_chat.has_value()
                                     && chat.peer_chat_main_id.has_value()
                                     && message->message.data.has_value()) {
                                     auto join = Json::deserialize<Chat::MessageJoinData>(
                                         message->message.data.value());
                                     if (join.has_value()) {
                                         auto peer_main = this->node->actor_index()->read_actor(
                                             chat.peer_chat_main_id.value());
                                         if (peer_main.has_value()) {
                                             auto bind_data =
                                                 ByteArray(join->per_chat.key().public_key()).toBytes();
                                             auto verify = peer_main->key().verify(bind_data,
                                                                                    join->bind_signature);
                                             if (verify.has_value() && verify.value()) {
                                                 chat.peer_per_chat       = join->per_chat;
                                                 chat.peer_bind_signature = join->bind_signature;
                                                 this->update_chat_in_mychats(chat);
                                             }
                                         }
                                     }
                                 }

                                 emit this->node->messageAdded(owner_id, dir_row.file_id, message.value());
                             }
                         }
                     });

    QObject::connect(node->dfs(),
                     &DfsController::vectorRowRemoved,
                     [this](ActorId owner_id, Dfs::DirRow dir_row, DbRow row) {
                         for (const auto& chat : std::as_const(chats_)) {
                             if ((chat.owner_id == owner_id || chat.chat.peer_id == owner_id)
                                 && chat.file_id == dir_row.file_id) {
                                 emit this->node->messageRemoved(owner_id, dir_row.file_id, row["id"]);
                             }
                         }
                     });
}

void ChatManager::set_mode(ChatMode mode) {
    mode_ = mode;
}

ChatMode ChatManager::mode() const {
    return mode_;
}

bool ChatManager::activated() const {
    return activated_;
}

std::expected<void, ChatError> ChatManager::activate() {
    if (activated_) {
        return {};
    }
    if (mode_ != ChatMode::Enabled) {
        return std::unexpected(ChatError::Disabled);
    }

    auto actor = node->account_controller()->chat_actor();
    if (!actor.has_value()) {
        return std::unexpected(ChatError::NoChatActor);
    }

    auto chat_actor_id = actor->get().id();

    // Broadcast chat_main to actor_index if not yet known (first activation in network).
    if (!node->actor_index()->exists(chat_actor_id)) {
        node->actor_index()->store_new_actor(actor->get().to_public());
        eLog("[Chat] activate: broadcast chat_main {}", chat_actor_id);
    }

    activated_ = true;

    // No eager profile row here: on a restored account the network's ChatProfile
    // arrives with the dirs sync later, and creating one now makes a duplicate.
    // set_name/set_bio/set_avatar create the row lazily when actually writing.

    auto db       = node->dfs()->get_db_instance();
    auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db, chat_actor_id);
    if (dir_rows.has_value()) {
        for (const auto& dir_row : dir_rows.value()) {
            this->parse_invite(chat_actor_id, dir_row);
        }
    }

    return {};
}

ActorId ChatManager::current_chat_actor_id() {
    auto actor = current_chat_actor();
    if (!actor.has_value()) {
        return ActorId();
    }
    return actor->get().id();
}

ActorId ChatManager::my_chat_main_id() {
    return current_chat_actor_id();
}

std::expected<std::reference_wrapper<const Actor<KeyPrivate>>, ChatError> ChatManager::current_chat_actor() {
    auto actor = node->account_controller()->chat_actor();
    if (!actor.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    return actor.value();
}

std::expected<Chat::Chat, ChatError> ChatManager::create_chat(bool encryption) {
    KeyBytes   key           = Cryptography::keygen();
    const auto chat_actor_id = current_chat_actor_id();
    if (chat_actor_id.is_zero()) {
        return std::unexpected(ChatError::NoChatActor);
    }

    auto db_instance = node->dfs()->get_db_instance();
    auto rows        = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_instance, chat_actor_id);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(db_instance,
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          "Chat");
    if (!search_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    // Per-chat actor — derived from seed with chat_key as label, recoverable from seed phrase.
    auto chat_key_label = Utils::to_hex(ByteArray(key).toBytes());
    auto per_chat = node->account_controller()->create_actor(ActorId(), chat_key_label, ActorType::User);
    if (per_chat.empty()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto chat = Chat::Chat { .chat_key       = key,
                             .my_per_chat_id = per_chat.id() };

    auto security_key = Dfs::DataSecurityKey { .key = chat.chat_key.value() };
    auto store_chat_res =
        encryption ? node->dfs()->store_vector(per_chat.id(),
                                               per_chat.id(),
                                               fmt::format("chat-{}",
                                                           node->dfs()->create_file_id_from("chat").substr(0, 10)),
                                               network_id,
                                               search_result->file_id,
                                               Dfs::DataSecurity::Key,
                                               security_key)
                   : node->dfs()->store_vector(per_chat.id(),
                                               per_chat.id(),
                                               fmt::format("channel-{}",
                                                           node->dfs()->create_file_id_from("chat").substr(0, 10)),
                                               network_id,
                                               search_result->file_id,
                                               Dfs::DataSecurity::Public);

    if (!store_chat_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    chat.owner_id = store_chat_res->actor_id;
    chat.file_id  = store_chat_res->file_id;

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::create_myself() {
    auto chat = create_chat();

    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    insert_chat_to_mychats(chat.value());
    add_new_message_created(chat->owner_id, chat->file_id);
    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::create_dialogue(ActorId with) {
    if (with.is_zero()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto chat = create_chat();

    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    chat->chat.peer_id       = with;
    chat->peer_chat_main_id  = with;
    insert_chat_to_mychats(chat.value());
    add_new_message_created(chat->owner_id, chat->file_id);
    invite(chat.value());
    add_new_message_invite(chat->owner_id, chat->file_id, with);

    auto custom = ThothCustom { .ignored = { chat->my_per_chat_id.value_or(current_chat_actor_id()) } };
    node->thoth_manager()->add_thoth_record(chat->owner_id, chat->file_id, Json::serialize(custom));

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::invite(const Chat::Chat& chat) {
    auto chat_main_result = current_chat_actor();
    if (!chat_main_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    const auto& chat_main = chat_main_result->get();

    if (!chat.my_per_chat_id.has_value() || !chat.chat_key.has_value()
        || !chat.peer_chat_main_id.has_value()) {
        return chat;
    }

    auto per_chat_actor_result =
        node->account_controller()->current_profile().get_actor(chat.my_per_chat_id.value());
    if (!per_chat_actor_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    const auto& per_chat = per_chat_actor_result->get();

    auto bind_data      = ByteArray(per_chat.key().public_key()).toBytes();
    auto bind_signature = chat_main.key().sign(bind_data);
    if (!bind_signature.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto invite = Chat::ChatInvite { .owner_id            = chat.owner_id,
                                     .file_id             = chat.file_id,
                                     .chat_type           = chat.chat.chat_type,
                                     .chat_key            = chat.chat_key.value(),
                                     .sender_chat_main_id = chat_main.id(),
                                     .sender_per_chat     = per_chat.to_public(),
                                     .bind_signature      = bind_signature.value() };

    auto json = Json::serialize(invite);
    // Privacy: sign/encrypt with per-chat actor so the replicated DirRow author is
    // anonymous, not chat_main. Sender chat_main stays inside the payload. TODO
    // (todo.md): use a throwaway invite-actor distinct from the chat vector owner.
    auto res =
        node->dfs()->store_data_as_file(chat.peer_chat_main_id.value(),
                                        per_chat.id(),
                                        ByteArray(json).toBytes(),
                                        CHAT_DAPP_INVITE_FOLDER,
                                        fmt::format("Invite_{}", Utils::generate_random_hex(8)),
                                        Dfs::DataSecurity::Actor,
                                        Dfs::DataSecurityActor { .sender_id   = per_chat.id(),
                                                                 .receiver_id = chat.peer_chat_main_id.value() });

    if (!res.has_value()) {
        eCritical("[ChatManager] Invite error: {}", res.error());
        return std::unexpected(ChatError::Unknown);
    }

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::create_channel(const std::string &name) {
    const auto chat_actor_id = current_chat_actor_id();
    if (chat_actor_id.is_zero()) {
        return std::unexpected(ChatError::NoChatActor);
    }

    auto db_instance = node->dfs()->get_db_instance();
    auto rows        = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_instance, chat_actor_id);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    // Without the synced Channels vector the channel would never appear publicly.
    if (!channels_vector_row().has_value()) {
        return std::unexpected(ChatError::NoChannelsVector);
    }

    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(db_instance,
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          "Chat");
    if (!search_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto chat     = Chat::Chat {};
    chat.chat_key = Cryptography::keygen();

    // Per-channel actor — derived from seed with chat_key as label, used only for derivation.
    auto label    = Utils::to_hex(ByteArray(chat.chat_key.value()).toBytes());
    auto per_chat = node->account_controller()->create_actor(ActorId(), label, ActorType::User);
    if (per_chat.empty()) {
        return std::unexpected(ChatError::Unknown);
    }
    chat.my_per_chat_id = per_chat.id();

    auto channel_hash = node->dfs()->create_file_id_from(
        fmt::format("{}{}{}", name, Utils::current_date_ms(), per_chat.id().to_string())).substr(0, 10);
    auto channel_name = fmt::format("Channel-{}", channel_hash);

    // Create channel vector (public, owned by per-channel actor)
    auto store_res = node->dfs()->store_vector(per_chat.id(),
                                               per_chat.id(),
                                               channel_name,
                                               network_id,
                                               search_result->file_id,
                                               Dfs::DataSecurity::Public);
    if (!store_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    chat.owner_id        = store_res->actor_id;
    chat.file_id         = store_res->file_id;
    chat.chat.chat_type  = Chat::ChatType::Channel;

    // Create metadata dictionary (public, same naming + "-meta")
    auto meta_name = fmt::format("{}-meta", channel_name);
    auto dict_res  = node->dfs()->store_dictionary(per_chat.id(), per_chat.id(), meta_name);
    if (dict_res.has_value() && !name.empty()) {
        node->dfs()->dictionary_set_value(per_chat.id(), dict_res->file_id, "name", name, per_chat.id());
    }

    // Publish to the public Channels vector so peers can discover and subscribe.
    if (!publish_channel(chat, name)) {
        eWarning("[Chat] Channel {} was not published to the public channels list", chat.file_id);
    }

    insert_chat_to_mychats(chat);
    add_new_message_created(chat.owner_id, chat.file_id);

    return chat;
}

std::expected<Dfs::DirRow, Dfs::DfsError> ChatManager::channels_vector_row() {
    return Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        node->dfs()->get_db_instance(),
        ActorId(CHAT_SERVICE_ACTOR),
        Dfs::Basic::TEMPLATE_VECTOR,
        ExtraChainNode::CHANNELS_VECTOR_NAME);
}

bool ChatManager::publish_channel(const Chat::Chat &chat, const std::string &name) {
    auto channels_row = channels_vector_row();
    if (!channels_row.has_value()) {
        return false;
    }

    auto signer_id = chat.my_per_chat_id.has_value() ? chat.my_per_chat_id.value() : current_chat_actor_id();
    return node->dfs()->add_vector_row(channels_row->owner_id,
                                       channels_row->file_id,
                                       { { "name", name },
                                         { "owner_id", chat.owner_id.to_string() },
                                         { "file_id", chat.file_id } },
                                       signer_id);
}

std::expected<std::vector<Chat::ChannelInfo>, ChatError> ChatManager::read_channels() {
    auto channels_row = channels_vector_row();
    if (!channels_row.has_value()) {
        return std::unexpected(ChatError::NoChannelsVector);
    }

    auto rows = node->dfs()->read_vector_rows(channels_row->owner_id, channels_row->file_id);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::vector<Chat::ChannelInfo> channels;
    for (auto &row : rows.value()) {
        // Only the channel owner may list it (signature already verified by DFS).
        if (row.count("actor") && row["actor"] != row["owner_id"]) {
            continue;
        }
        channels.push_back(Chat::ChannelInfo { .owner_id = ActorId(row["owner_id"]),
                                               .file_id  = row["file_id"],
                                               .name     = row.count("name") ? row["name"] : "" });
    }

    return channels;
}

std::optional<std::string> ChatManager::get_channel_name(const Chat::Chat &chat) {
    auto db_instance   = node->dfs()->get_db_instance();
    auto channel_row   = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, chat.owner_id, chat.file_id);
    if (!channel_row.has_value() || channel_row->name.empty()) {
        return std::nullopt;
    }

    auto meta_name = channel_row->name + "-meta";
    auto meta_row  = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, chat.owner_id, meta_name, "name");
    if (!meta_row.has_value()) {
        return std::nullopt;
    }

    return node->dfs()->read_dictionary(chat.owner_id, meta_row->file_id, "name");
}

bool ChatManager::set_channel_name(const Chat::Chat &chat, const std::string &name) {
    auto db_instance   = node->dfs()->get_db_instance();
    auto channel_row   = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, chat.owner_id, chat.file_id);
    if (!channel_row.has_value() || channel_row->name.empty()) {
        return false;
    }

    auto meta_name = channel_row->name + "-meta";
    auto meta_row  = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, chat.owner_id, meta_name, "name");
    if (!meta_row.has_value()) {
        return false;
    }

    auto author_id = chat.my_per_chat_id.has_value() ? chat.my_per_chat_id.value() : current_chat_actor_id();
    return node->dfs()->dictionary_set_value(chat.owner_id, meta_row->file_id, "name", name, author_id);
}

std::expected<Chat::Chat, ChatError> ChatManager::subscribe_channel(const ActorId&     owner_id,
                                                                     const std::string& file_id) {
    auto chat      = Chat::Chat { .owner_id = owner_id,
                                  .file_id  = file_id,
                                  .chat     = Chat::ChatData { .chat_type = Chat::ChatType::Channel } };
    auto my_result = insert_chat_to_mychats(chat);
    if (!my_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    node->dfs()->request_file(owner_id, file_id);

    auto custom = ThothCustom { .ignored = { current_chat_actor_id() } };
    node->thoth_manager()->add_thoth_record(chat.owner_id, chat.file_id, Json::serialize(custom));

    return chat;
}

std::expected<std::vector<Chat::Chat>, ChatError> ChatManager::read_chats() {
    auto my_chats = this->read_my_chats_row();

    if (!my_chats.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto security_actor = Dfs::DataSecuritySelf { .my_actor = current_chat_actor_id() };
    auto rows = node->dfs()->read_dictionary_rows(my_chats->actor_id, my_chats->file_id, security_actor);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::vector<Chat::Chat> chats;
    chats.reserve(rows->size());
    std::set<std::pair<std::string, std::string>> seen_chat_links;

    for (const auto& [key, value] : rows.value()) {
        auto chat = Json::deserialize<Chat::Chat>(value);
        if (!chat.has_value()) {
            continue;
        }

        if (!is_valid_chat_link(chat.value())) {
            eWarning("[Chat] Skip invalid chat row: key={}, owner={}, file={}",
                     key,
                     chat->owner_id,
                     chat->file_id);
            continue;
        }

        auto chat_link = std::make_pair(chat->owner_id.to_string(), chat->file_id);
        if (!seen_chat_links.insert(chat_link).second) {
            eWarning("[Chat] Skip duplicate chat row: key={}, owner={}, file={}",
                     key,
                     chat->owner_id,
                     chat->file_id);
            continue;
        }

        // Restore per-chat keypair only if I am a participant who can sign in this chat.
        if (chat.value().my_per_chat_id.has_value() && chat.value().chat_key.has_value()) {
            auto label = Utils::to_hex(ByteArray(chat.value().chat_key.value()).toBytes());
            node->account_controller()->restore_actor(ActorId(), label, ActorType::User);
        }

        mark_chat_priority(chat.value());
        chats.push_back(chat.value());
    }

    chats_ = chats;

    // Chat list is ready: (re)register my push token per chat (guarded + deduped inside).
    node->thoth_manager()->reconcile_tokens_for_chats(chats);

    return chats;
}

std::expected<std::vector<Chat::Message>, ChatError> ChatManager::read_chat_messages(const ActorId&     owner_id,
                                                                                     const std::string& file_id,
                                                                                     bool               quick) {
    auto chat = this->get_chat(owner_id, file_id);
    if (!quick && !chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    bool encryption = !quick && chat.value().chat_key.has_value();
    if (!quick && chat.value().chat.chat_type.has_value()
        && chat.value().chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
    }

    Dfs::DataSecurityData security_key;
    if (encryption) {
        security_key = Dfs::DataSecurityKey { .key = chat.value().chat_key.value() };
    }

    auto db_rows = node->dfs()->read_vector_rows(owner_id,
                                                 file_id,
                                                 "where status = '1' ORDER by timestamp",
                                                 encryption ? security_key : Dfs::DataSecurityData());

    if (!db_rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::vector<Chat::Message> messages;
    messages.reserve(db_rows->size());

    for (const auto& db_row : db_rows.value()) {
        auto rown = db_row;
        rown.erase("sign");
        rown.erase("status");

        auto message = Utils::from_dbrow<Chat::Message>(rown);
        if (!message.has_value()) {
            continue;
        }
        messages.push_back(message.value());
    }

    return messages;
}

std::expected<Chat::Message, ChatError> ChatManager::read_last_message(const ActorId&     owner_id,
                                                                       const std::string& file_id) {
    auto chat = this->get_chat(owner_id, file_id);
    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    bool encryption = chat.value().chat_key.has_value();
    if (chat.value().chat.chat_type.has_value() && chat.value().chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
    }

    Dfs::DataSecurityData security_key;
    if (encryption) {
        security_key = Dfs::DataSecurityKey { .key = chat.value().chat_key.value() };
    }

    constexpr int batch_size = 10;
    Chat::Message best;
    std::uint64_t best_ts = 0;
    bool found_non_edited = false;

    // "Delete for me" hides the message only on the author's side (same rule as UI).
    auto my_id = chat.value().my_per_chat_id.has_value() ? chat.value().my_per_chat_id.value()
                                                         : current_chat_actor_id();

    for (int offset = 0; offset < 100; offset += batch_size) {
        auto query   = fmt::format("where status = '1' ORDER by timestamp DESC LIMIT {} OFFSET {}", batch_size, offset);
        auto db_rows = node->dfs()->read_vector_rows(owner_id, file_id, query,
                                                     encryption ? security_key : Dfs::DataSecurityData());
        if (!db_rows.has_value() || db_rows->empty()) {
            break;
        }

        for (auto &db_row : db_rows.value()) {
            db_row.erase("sign");
            db_row.erase("status");

            auto message = Utils::from_dbrow<Chat::Message>(db_row);
            if (!message.has_value()) {
                continue;
            }

            if (message->message.deleted_for_me.value_or(false) && message->actor == my_id) {
                continue;
            }

            auto effective_ts = message->message.original_timestamp.value_or(message->timestamp);

            if (effective_ts >= best_ts) {
                best    = message.value();
                best_ts = effective_ts;
            }

            if (!message->message.original_timestamp.has_value()) {
                found_non_edited = true;
            }
        }

        if (found_non_edited) {
            break;
        }

        if (static_cast<int>(db_rows->size()) < batch_size) {
            break;
        }
    }

    if (best_ts > 0) {
        return best;
    }

    return std::unexpected(ChatError::Unknown);
}

std::expected<bool, ChatError> ChatManager::add_new_message(const ActorId&       owner_id,
                                                            const std::string&   file_id,
                                                            const Chat::Message& message) {
    auto chat = get_chat(owner_id, file_id);
    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto signer_id = chat.value().my_per_chat_id.has_value() ? chat.value().my_per_chat_id.value()
                                                              : current_chat_actor_id();

    bool encryption = chat.value().chat_key.has_value();
    if (chat.value().chat.chat_type.has_value() && chat.value().chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
        if (chat.value().owner_id != signer_id) {
            return std::unexpected(ChatError::Unknown);
        }
    }

    Dfs::DataSecurityData security_key;
    if (encryption) {
        security_key = Dfs::DataSecurityKey { .key = chat.value().chat_key.value() };
    }
    auto res = node->dfs()->add_vector_row(owner_id, file_id, message, signer_id, security_key, true);

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    // TODO: emit messageAdded locally so the sender sees its own message before sync.
    return res;
}

std::expected<bool, ChatError> ChatManager::add_new_message_text(const ActorId&           owner_id,
                                                                 const std::string&       file_id,
                                                                 const Chat::MessageText& message_text) {
    auto text = Utils::trim(Utils::sanitize_text(message_text.text));
    if (text.empty()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto message_data = Chat::MessageData { .data = text, .reply_id = message_text.reply_id };
    message_data.type = Chat::MessageType::Text;
    auto message = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    // TODO: with id exists check
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_new_message_created(const ActorId&     owner_id,
                                                                    const std::string& file_id) {
    // TODO: check size, only if size == 0

    auto message_data = Chat::MessageData { .type = Chat::MessageType::Created };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_new_message_invite(const ActorId&     owner_id,
                                                                   const std::string& file_id,
                                                                   const ActorId&     actor) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::Invite, .data = actor.to_string() };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_new_message_joined(const ActorId&     owner_id,
                                                                   const std::string& file_id,
                                                                   const ActorId&     actor) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::Join, .data = actor.to_string() };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_gif_message(const ActorId&           owner_id,
                                                            const std::string&       file_id,
                                                            const Chat::MessageText& message_text) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::Gif, .data = message_text.text };
    auto message = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    // TODO: with id exists check
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_image_message(const ActorId&           owner_id,
                                                              const std::string&       file_id,
                                                              const Chat::MessageText& message_text) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::Image, .data = message_text.text };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_video_message(const ActorId&           owner_id,
                                                              const std::string&       file_id,
                                                              const Chat::MessageText& message_text) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::Video, .data = message_text.text };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_audio_message(const ActorId&           owner_id,
                                                              const std::string&       file_id,
                                                              const Chat::MessageText& message_text) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::Audio, .data = message_text.text };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::add_file_message(const ActorId&           owner_id,
                                                             const std::string&       file_id,
                                                             const Chat::MessageText& message_text) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::File, .data = message_text.text };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::edit_message(const ActorId&     owner_id,
                                                         const std::string& file_id,
                                                         const std::string& message_id,
                                                         const std::string& new_text) {
    auto text = Utils::trim(Utils::sanitize_text(new_text));

    auto chat = get_chat(owner_id, file_id);
    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto signer_id = chat.value().my_per_chat_id.has_value() ? chat.value().my_per_chat_id.value()
                                                              : current_chat_actor_id();

    bool encryption = chat.value().chat_key.has_value();
    if (chat.value().chat.chat_type.has_value() && chat.value().chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
        if (chat.value().owner_id != signer_id) {
            return std::unexpected(ChatError::Unknown);
        }
    }

    // Read original message to preserve its timestamp
    auto messages = read_chat_messages(owner_id, file_id);
    if (!messages.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::uint64_t original_timestamp = 0;
    Chat::MessageData original_data;
    bool found = false;
    for (const auto &msg : messages.value()) {
        if (msg.id == message_id) {
            original_timestamp = msg.timestamp;
            if (msg.message.original_timestamp.has_value()) {
                original_timestamp = msg.message.original_timestamp.value();
            }
            original_data = msg.message;
            found = true;
            break;
        }
    }

    if (!found) {
        return std::unexpected(ChatError::Unknown);
    }

    std::string new_data;
    auto msg_type = original_data.type.value_or(Chat::MessageType::Text);

    if (msg_type == Chat::MessageType::Text) {
        if (text.empty()) {
            return std::unexpected(ChatError::Unknown);
        }
        new_data = text;
    } else {
        // Media message — update caption in JSON, keep path/hash/etc
        auto original_json = boost::json::parse(original_data.data.value_or("{}"));
        if (original_json.is_object()) {
            original_json.as_object()["caption"] = text;
            new_data = boost::json::serialize(original_json);
        } else {
            new_data = text;
        }
    }

    auto message_data = Chat::MessageData { .type = msg_type, .data = new_data, .original_timestamp = original_timestamp };
    auto message      = Chat::Message { .id = message_id, .message = message_data };

    Dfs::DataSecurityData security_key;
    if (encryption) {
        security_key = Dfs::DataSecurityKey { .key = chat.value().chat_key.value() };
    }
    auto res = node->dfs()->update_vector_row(owner_id, file_id, message, signer_id, security_key);

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    return res;
}

std::expected<bool, ChatError> ChatManager::remove_message(const ActorId&     owner_id,
                                                           const std::string& file_id,
                                                           const std::string& message_id) {
    auto chat = get_chat(owner_id, file_id);
    ActorId signer_id;
    if (chat.has_value() && chat.value().my_per_chat_id.has_value()) {
        signer_id = chat.value().my_per_chat_id.value();
    } else {
        signer_id = current_chat_actor_id();
    }
    auto res = node->dfs()->remove_vector_row(owner_id, file_id, message_id, signer_id);

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    emit node->messageRemoved(owner_id, file_id, message_id);
    return res;
}

std::expected<bool, ChatError> ChatManager::delete_for_me(const ActorId&     owner_id,
                                                          const std::string& file_id,
                                                          const std::string& message_id) {
    auto chat = get_chat(owner_id, file_id);
    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    // Channels allow only full removal: posts are public, "hide for me" is moot.
    if (chat.value().chat.chat_type.has_value() && chat.value().chat.chat_type == Chat::ChatType::Channel) {
        return std::unexpected(ChatError::NotAllowed);
    }

    auto signer_id = chat.value().my_per_chat_id.has_value() ? chat.value().my_per_chat_id.value()
                                                              : current_chat_actor_id();

    auto messages = read_chat_messages(owner_id, file_id);
    if (!messages.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    Chat::Message original;
    bool          found = false;
    for (const auto& msg : messages.value()) {
        if (msg.id == message_id) {
            original = msg;
            found    = true;
            break;
        }
    }
    if (!found) {
        return std::unexpected(ChatError::Unknown);
    }

    if (original.actor != signer_id) {
        return std::unexpected(ChatError::Unknown);
    }

    bool encryption = chat.value().chat_key.has_value();
    if (chat.value().chat.chat_type.has_value() && chat.value().chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
    }

    Chat::MessageData updated_data    = original.message;
    updated_data.deleted_for_me       = true;

    Chat::Message updated { .id        = message_id,
                            .timestamp = original.timestamp,
                            .actor     = original.actor,
                            .message   = updated_data };

    Dfs::DataSecurityData security_key;
    if (encryption) {
        security_key = Dfs::DataSecurityKey { .key = chat.value().chat_key.value() };
    }
    auto res = node->dfs()->update_vector_row(owner_id, file_id, updated, signer_id, security_key);
    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }
    return res;
}

std::expected<Dfs::DirRow, ChatError> ChatManager::create_mychats() {
    auto my_chats_result = this->read_my_chats_row();
    if (my_chats_result.has_value()) {
        return my_chats_result.value();
    }

    auto chat_actor_id  = current_chat_actor_id();
    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_id };

    auto store_chats_res = node->dfs()->store_dictionary(chat_actor_id,
                                                          chat_actor_id,
                                                          CHAT_MY_CHATS_INFO,
                                                          Dfs::DataSecurity::Self,
                                                          security_actor);

    if (!store_chats_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    return store_chats_res.value();
}

std::expected<Dfs::DirRow, ChatError> ChatManager::read_my_chats_row() {
    if (!my_chats_row_.empty()) {
        return my_chats_row_;
    }

    auto chat_actor_result = current_chat_actor();
    if (!chat_actor_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    const auto& chat_actor    = chat_actor_result->get();
    const auto  chat_actor_id = chat_actor.id();

    auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(node->dfs()->get_db_instance(), chat_actor_id);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    for (const auto& row : rows.value()) { // TODO: need normal search (as function)
        if (row.folder != Dfs::Basic::TEMPLATE_DICTIONARY) {
            continue;
        }

        auto from_base64 = Utils::from_base64(row.name);
        if (!from_base64.has_value()) {
            continue;
        }

        auto name_result = chat_actor.key().decrypt_self(ByteArray(from_base64.value()).toBytes());
        if (!name_result.has_value()) {
            continue;
        }

        auto name = ByteArray(name_result.value()).toString();
        if (name == CHAT_MY_CHATS_INFO) {
            my_chats_row_ = row;
            break;
        }
    }

    if (my_chats_row_.empty()) {
        return std::unexpected(ChatError::Unknown);
    }

    return my_chats_row_;
}

std::expected<bool, ChatError> ChatManager::insert_chat_to_mychats(const Chat::Chat& chat) {
    if (!is_valid_chat_link(chat)) {
        eWarning("[Chat] Refuse to insert invalid chat row: owner={}, file={}", chat.owner_id, chat.file_id);
        return std::unexpected(ChatError::Unknown);
    }

    auto existing_cached = std::find_if(chats_.cbegin(), chats_.cend(), [&chat](const auto& current) {
        return same_chat_link(current, chat);
    });
    if (existing_cached != chats_.cend()) {
        return true;
    }

    auto my_chats = this->read_my_chats_row();
    if (!my_chats.has_value()) {
        auto my_chats_result = create_mychats();
        if (!my_chats_result.has_value()) {
            return std::unexpected(ChatError::Unknown);
        }
        my_chats = my_chats_result;
    }

    auto security_actor = Dfs::DataSecuritySelf { .my_actor = current_chat_actor_id() };
    auto rows = node->dfs()->read_dictionary_rows(my_chats->actor_id, my_chats->file_id, security_actor);
    if (rows.has_value()) {
        for (const auto& [key, value] : rows.value()) {
            auto existing = Json::deserialize<Chat::Chat>(value);
            if (existing.has_value() && is_valid_chat_link(existing.value())
                && same_chat_link(existing.value(), chat)) {
                chats_.push_back(existing.value());
                mark_chat_priority(existing.value());
                return true;
            }
        }
    }

    auto chat_new = chat;
    chat_new.id   = Utils::generate_random_hex(6);

    auto chat_actor_id  = current_chat_actor_id();
    auto value          = Json::serialize(chat_new);
    auto res = node->dfs()->dictionary_set_value(chat_actor_id, my_chats->file_id, chat_new.id, value,
                                                  chat_actor_id, security_actor);

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    mark_chat_priority(chat_new);
    chats_.push_back(chat_new);
    emit node->chatAdded(chat_new);

    return res;
}

std::expected<bool, ChatError> ChatManager::update_chat_in_mychats(const Chat::Chat& chat) {
    auto my_chats = this->read_my_chats_row();
    if (!my_chats.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    if (chat.id.empty() || !is_valid_chat_link(chat)) {
        eWarning("[Chat] Refuse to update invalid chat row: id={}, owner={}, file={}",
                 chat.id,
                 chat.owner_id,
                 chat.file_id);
        return std::unexpected(ChatError::Unknown);
    }

    auto chat_actor_id  = current_chat_actor_id();
    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_id };
    auto value          = Json::serialize(chat);
    auto res = node->dfs()->dictionary_set_value(chat_actor_id, my_chats->file_id, chat.id, value,
                                                  chat_actor_id, security_actor);

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }
    return res;
}

void ChatManager::mark_chat_priority(const Chat::Chat& chat) {
    node->dfs()->add_priority_file_link({ chat.owner_id, chat.file_id });
    if (chat.peer_chat_main_id.has_value()) {
        node->dfs()->add_priority_actor(chat.peer_chat_main_id.value());
    }
}

std::optional<Chat::Chat> ChatManager::get_chat(const ActorId& owner_id, const std::string& file_id) {
    for (const auto& chat : std::as_const(chats_)) {
        if (chat.owner_id == owner_id && chat.file_id == file_id) {
            return chat;
        }
    }

    return std::nullopt;
}

void ChatManager::update_dfs_files() {
    for (const auto& chat : chats_) {
        node->dfs()->request_file(chat.owner_id, chat.file_id);
    }
}

bool ChatManager::parse_invite(const ActorId& owner_id, const Dfs::DirRow& dir_row) {
    if (dir_row.folder != CHAT_DAPP_INVITE_FOLDER) {
        return false;
    }

    if (!dir_row.encryption) {
        return false;
    }

    auto encrypted = Dfs::Tables::DirsFile::ActorSpace::get_file_content(owner_id, dir_row.file_id);
    if (!encrypted.has_value()) {
        eWarning("[Chat] parse_invite {}: no file content", dir_row.file_id);
        return false;
    }

    const auto& from_id  = dir_row.actor_id;
    auto recipient_result = this->node->account_controller()->current_profile().get_actor(owner_id);
    if (!recipient_result.has_value()) {
        eWarning("[Chat] parse_invite {}: recipient {} not in profile", dir_row.file_id, owner_id);
        return false;
    }
    const auto& recipient = recipient_result->get();

    auto from_actor_result = this->node->actor_index()->read_actor(from_id);
    if (!from_actor_result.has_value()) {
        eWarning("[Chat] parse_invite {}: sender {} not in actor index", dir_row.file_id, from_id);
        return false;
    }
    auto from_actor = from_actor_result.value();

    auto content = recipient.key().decrypt(encrypted.value(), from_actor.key().public_key());
    if (!content.has_value()) {
        eWarning("[Chat] parse_invite {}: decrypt failed (from {})", dir_row.file_id, from_id);
        return false;
    }
    auto chat_invite = Json::deserialize<Chat::ChatInvite>(content.value());
    if (!chat_invite.has_value()) {
        eWarning("[Chat] parse_invite {}: deserialize failed", dir_row.file_id);
        return false;
    }

    // Invite is signed by the sender's per-chat actor, so the DFS author must match
    // the per_chat claimed in the payload, not chat_main.
    if (dir_row.actor_id != chat_invite->sender_per_chat.id()) {
        eWarning("[Chat] parse_invite {}: author {} != payload per_chat {}",
                 dir_row.file_id,
                 dir_row.actor_id,
                 chat_invite->sender_per_chat.id());
        return false;
    }

    // Verify per_chat ↔ chat_main bind: signed by chat_main, resolved from actor index.
    auto sender_chat_main = this->node->actor_index()->read_actor(chat_invite->sender_chat_main_id);
    if (!sender_chat_main.has_value()) {
        eWarning("[Chat] parse_invite {}: sender chat_main {} not in actor index",
                 dir_row.file_id,
                 chat_invite->sender_chat_main_id);
        return false;
    }
    auto bind_data = ByteArray(chat_invite->sender_per_chat.key().public_key()).toBytes();
    auto verify    = sender_chat_main->key().verify(bind_data, chat_invite->bind_signature);
    if (!verify.has_value() || !verify.value()) {
        eWarning("[Chat] parse_invite {}: bind signature verify failed", dir_row.file_id);
        return false;
    }

    // Create my per_chat actor — derived from seed with chat_key as label.
    auto my_per_chat = node->account_controller()->create_actor(
        ActorId(), Utils::to_hex(ByteArray(chat_invite->chat_key).toBytes()), ActorType::User);
    if (my_per_chat.empty()) {
        eWarning("[Chat] parse_invite {}: create per_chat actor failed", dir_row.file_id);
        return false;
    }

    auto chat =
        Chat::Chat { .id                  = "",
                     .owner_id            = chat_invite->owner_id,
                     .file_id             = chat_invite->file_id,
                     .chat                = Chat::ChatData { .chat_type = chat_invite->chat_type,
                                                              .peer_id  = chat_invite->sender_chat_main_id },
                     .chat_key            = chat_invite->chat_key,
                     .my_per_chat_id      = my_per_chat.id(),
                     .peer_chat_main_id   = chat_invite->sender_chat_main_id,
                     .peer_per_chat       = chat_invite->sender_per_chat,
                     .peer_bind_signature = chat_invite->bind_signature };

    // TODO: check if myself == myself? if i have ~ devices

    auto mychats_insert_result = this->insert_chat_to_mychats(chat);
    if (!mychats_insert_result.has_value()) {
        return false;
    }

    this->node->dfs()->remove_stored_file(owner_id, dir_row.file_id);

    // Join message — carries my per_chat public key + bind so sender learns who I am.
    auto chat_main_result = current_chat_actor();
    if (chat_main_result.has_value()) {
        auto my_bind_data = ByteArray(my_per_chat.key().public_key()).toBytes();
        auto my_bind      = chat_main_result->get().key().sign(my_bind_data);
        if (my_bind.has_value()) {
            Chat::MessageJoinData join_data { .per_chat = my_per_chat.to_public(),
                                              .bind_signature = my_bind.value() };
            auto                  join_json = Json::serialize(join_data);
            auto message_data = Chat::MessageData { .type = Chat::MessageType::Join, .data = join_json };
            auto message      = Chat::Message { .id      = Utils::generate_random_hex(6),
                                                .actor   = my_per_chat.id(),
                                                .message = message_data };
            add_new_message(chat.owner_id, chat.file_id, message);
        }
    }

    auto custom = ThothCustom { .ignored = { chat.my_per_chat_id.value_or(current_chat_actor_id()) } };
    node->thoth_manager()->add_thoth_record(chat.owner_id, chat.file_id, Json::serialize(custom));

    return true;
}
