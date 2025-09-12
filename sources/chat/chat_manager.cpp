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

ChatManager::ChatManager(ExtraChainNode* node)
    : node(node) {
    QObject::connect(node->dfs(), &DfsController::downloaded, [this](ActorId owner_id, Dfs::DirRow dir_row) {
        if (this->chat_actor_ != owner_id) { // add my accounts?
            return;
        }

        auto my_chats = get_my_chats();
        if (my_chats.has_value()) {
            if (my_chats->file_id == dir_row.file_id) {
                emit this->node->chatsLoaded();
                return;
            }
        }

        // load my chats
        // emit this->node->chatLoaded(owner_id, file_id);

        this->parse_invite(owner_id, dir_row);
    });

    QObject::connect(node, &ExtraChainNode::ready, [this]() {
        auto main_id = this->node->account_controller()->currentProfile().main_id();

        auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(main_id); // TODO: add where / field
        if (!dir_rows.has_value()) {
            return;
        }

        for (const auto& dir_row : dir_rows.value()) {
            this->parse_invite(main_id, dir_row);
        }
    });

    QObject::connect(node->dfs(),
                     &DfsController::vectorRowAdded,
                     [this](ActorId owner_id, Dfs::DirRow dir_row, DbRow row) {
                         if (row["status"] != "1") {
                             return;
                         }

                         for (const auto& chat : std::as_const(chats_)) {
                             if ((chat.owner_id == owner_id || chat.chat.peer_id == owner_id)
                                 && chat.file_id == dir_row.file_id) {
                                 auto securiry_key = Dfs::DataSecurityKey { .key = chat.chat_key };
                                 bool encryption   = true;
                                 if (chat.chat.chat_type.has_value()
                                     && chat.chat.chat_type == Chat::ChatType::Channel) {
                                     encryption = false;
                                 }

                                 auto message_row =
                                     this->node->dfs()->get_vector_row(owner_id,
                                                                       dir_row.file_id,
                                                                       row["id"],
                                                                       encryption ? securiry_key
                                                                                  : Dfs::DataSecurityData());
                                 if (!message_row.has_value()) {
                                     return;
                                 }

                                 message_row->erase("sign");
                                 message_row->erase("status");
                                 auto message = Utils::from_dbrow<Chat::Message>(message_row.value());
                                 if (!message.has_value()) {
                                     return;
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

std::expected<Chat::Chat, ChatError> ChatManager::create_chat(bool encryption) {
    KeyBytes   key           = Cryptography::keygen();
    const auto main_actor_id = node->account_controller()->currentProfile().main_id();
    chat_actor_              = main_actor_id;

    // TODO: my actor = use actor for chats

    // ... check if chats is exists ...

    auto rows = Dfs::Tables::ActorDirFile::get_dir_rows(main_actor_id);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    // TODO: is_my_chats_exists -> create else return error Exists -> load chat in gui

    auto chat = Chat::Chat { .chat_key = key };

    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto search_result =
        Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(network_id,
                                                                  Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                  "Chat");
    if (!search_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto security_key = Dfs::DataSecurityKey { .key = chat.chat_key };
    auto store_chat_res =
        encryption ? node->dfs()->store_vector(main_actor_id,
                                               main_actor_id,
                                               fmt::format("chat-{}",
                                                           node->dfs()->create_file_id_from("chat").substr(0, 10)),
                                               network_id,
                                               search_result->file_id,
                                               Dfs::DataSecurity::Key,
                                               security_key)
                   : node->dfs()->store_vector(main_actor_id,
                                               main_actor_id,
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

    chat->chat.peer_id = with.to_string();
    insert_chat_to_mychats(chat.value());
    add_new_message_created(chat->owner_id, chat->file_id);
    invite(chat.value());
    add_new_message_invite(chat->owner_id, chat->file_id, with);

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::invite(const Chat::Chat& chat) {
    // check if with this person chat exists
    auto main_actor_id = node->account_controller()->currentProfile().main_id();

    if (!chat.chat.peer_id.has_value()) {
        return chat;
    }

    auto invite = Chat::ChatInvite { .owner_id  = chat.owner_id,
                                     .file_id   = chat.file_id,
                                     .chat_type = chat.chat.chat_type,
                                     .chat_key  = chat.chat_key };

    auto json = Json::serialize(invite);
    auto res =
        node->dfs()->store_data_as_file(chat.chat.peer_id.value(),
                                        chat_actor_,
                                        ByteArray(json).toBytes(),
                                        CHAT_DAPP_INVITE_FOLDER,
                                        fmt::format("From_{}", main_actor_id),
                                        Dfs::DataSecurity::Actor,
                                        Dfs::DataSecurityActor { .sender_id   = chat_actor_,
                                                                 .receiver_id = chat.chat.peer_id.value() });

    if (!res.has_value()) {
        eCritical("[ChatManager] Invite error: {}", res.error());
        return std::unexpected(ChatError::Unknown);
    }

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::create_channel() {
    auto chat = create_chat(false);

    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    chat->chat.chat_type = Chat::ChatType::Channel;
    insert_chat_to_mychats(chat.value());
    add_new_message_created(chat->owner_id, chat->file_id);

    return chat;
}

std::expected<std::vector<Chat::Chat>, ChatError> ChatManager::read_chats() {
    auto main_actor = node->account_controller()->currentProfile().main()->get();
    auto my_chats   = get_my_chats();

    if (!my_chats.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_ };
    auto rows =
        node->dfs()->get_vector_rows(my_chats->actor_id, my_chats->file_id, "where status = '1'", security_actor);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::vector<Chat::Chat> chats;
    chats.reserve(rows->size());

    for (const auto& row : rows.value()) {
        auto rown = row;
        rown.erase("actor");
        rown.erase("sign");
        rown.erase("timestamp");
        rown.erase("status");

        auto chat = Utils::from_dbrow<Chat::Chat>(rown);
        if (!chat.has_value()) {
            continue;
        }
        chats.push_back(chat.value());
    }

    chats_ = chats;
    return chats;
}

std::expected<std::vector<Chat::Message>, ChatError> ChatManager::read_chat_messages(const ActorId&     owner_id,
                                                                                     const std::string& file_id,
                                                                                     bool               quick) {
    auto chat = get_chat(owner_id, file_id);
    if (!quick && !chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto security_key = quick ? Dfs::DataSecurityData() : Dfs::DataSecurityKey { .key = chat->chat_key };

    bool encryption = true;
    if (!quick && chat->chat.chat_type.has_value() && chat->chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
    }

    auto db_rows = node->dfs()->get_vector_rows(owner_id,
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

std::expected<bool, ChatError> ChatManager::add_new_message(const ActorId&       owner_id,
                                                            const std::string&   file_id,
                                                            const Chat::Message& message) {
    auto chat = get_chat(owner_id, file_id);
    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    bool encryption = true;
    if (chat->chat.chat_type.has_value() && chat->chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
    }

    auto security_key = Dfs::DataSecurityKey { .key = chat->chat_key };
    auto res          = node->dfs()->add_vector_row(owner_id,
                                           file_id,
                                           message,
                                           chat_actor_,
                                           encryption ? security_key : Dfs::DataSecurityData());

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    // TODO: send full correct
    // message.actor     = node->accountController()->currentProfile().main_id();
    // message.timestamp = Utils::current_date_ms();
    // emit node->messageAdded(owner_id, file_id, message);
    return res;
}

std::expected<bool, ChatError> ChatManager::add_new_message_text(const ActorId&           owner_id,
                                                                 const std::string&       file_id,
                                                                 const Chat::MessageText& message_text) {
    // ... checks for file ...

    auto message_data = Chat::MessageData { .data = message_text.text, .reply_id = message_text.reply_id };
    // auto message_data_json = Json::serialize(message_data);
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
    // auto message_data_json = Json::serialize(message_data);
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

std::expected<bool, ChatError> ChatManager::add_file_message(const ActorId&           owner_id,
                                                             const std::string&       file_id,
                                                             const Chat::MessageText& message_text) {
    auto message_data = Chat::MessageData { .type = Chat::MessageType::File, .data = message_text.text };
    auto message      = Chat::Message { .id = Utils::generate_random_hex(6), .message = message_data };
    return add_new_message(owner_id, file_id, message);
}

std::expected<bool, ChatError> ChatManager::remove_message(const ActorId&     owner_id,
                                                           const std::string& file_id,
                                                           const std::string& message_id) {
    auto res = node->dfs()->remove_vector_row(owner_id, file_id, message_id, chat_actor_);

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    emit node->messageRemoved(owner_id, file_id, message_id);
    return res;
}

std::expected<Dfs::DirRow, ChatError> ChatManager::create_mychats() {
    auto my_chats_result = get_my_chats();
    if (my_chats_result.has_value()) {
        return my_chats_result.value();
    }

    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto search_result =
        Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(network_id,
                                                                  Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                  CHAT_MY_CHATS);
    if (!search_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto main_actor_id  = node->account_controller()->currentProfile().main_id();
    auto security_actor = Dfs::DataSecuritySelf { .my_actor = main_actor_id };

    auto store_chats_res = node->dfs()->store_vector(main_actor_id,
                                                     main_actor_id,
                                                     CHAT_MY_CHATS,
                                                     network_id,
                                                     search_result->file_id,
                                                     Dfs::DataSecurity::Self,
                                                     security_actor);

    if (!store_chats_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    return store_chats_res.value();
}

std::expected<Dfs::DirRow, ChatError> ChatManager::get_my_chats() {
    static Dfs::DirRow my_chats;
    if (!my_chats.empty()) {
        return my_chats;
    }

    auto main_actor_id = node->account_controller()->currentProfile().main_id();
    chat_actor_        = main_actor_id;
    auto rows          = Dfs::Tables::ActorDirFile::get_dir_rows(main_actor_id);
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    for (const auto& row : rows.value()) { // TODO: need normal search (as function)
        if (row.folder != Dfs::Basic::TEMPLATE_VECTOR) {
            continue;
        }

        auto from_base64 = Utils::from_base64(row.name);
        if (!from_base64.has_value()) {
            continue;
        }

        auto actor       = node->account_controller()->currentProfile().main()->get();
        auto name_result = actor.key().decrypt_self(ByteArray(from_base64.value()).toBytes());
        if (!name_result.has_value()) {
            continue;
        }

        auto name = ByteArray(name_result.value()).toString();
        if (name == CHAT_MY_CHATS) {
            my_chats = row;
            break;
        }
    }

    if (my_chats.empty()) {
        return std::unexpected(ChatError::Unknown);
    }

    return my_chats;
}

std::expected<bool, ChatError> ChatManager::insert_chat_to_mychats(const Chat::Chat& chat) {
    // TODO: checks if chat exists

    auto my_chats = get_my_chats();
    if (!my_chats.has_value()) {
        auto my_chats_result = create_mychats();
        if (!my_chats_result.has_value()) {
            return std::unexpected(ChatError::Unknown);
        }
        my_chats = my_chats_result;
    }

    auto chat_new = chat;
    chat_new.id   = Utils::generate_random_hex(6);

    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_ };
    auto res = node->dfs()->add_vector_row(chat_actor_, my_chats->file_id, chat_new, chat_actor_, security_actor);

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    chats_.push_back(chat);
    emit node->chatAdded(chat);

    return res;
}

std::optional<Chat::Chat> ChatManager::get_chat(const ActorId& owner_id, const std::string& file_id) {
    for (const auto& chat : std::as_const(chats_)) {
        if (chat.owner_id == owner_id && chat.file_id == file_id) {
            return chat;
        }
    }

    return std::nullopt;
}

bool ChatManager::parse_invite(const ActorId& owner_id, const Dfs::DirRow& dir_row) {

    if (dir_row.folder != CHAT_DAPP_INVITE_FOLDER) {
        return false;
    }

    if (!dir_row.encryption) {
        return false;
    }

    auto encrypted = Dfs::Tables::ActorDirFile::get_file_content(owner_id, dir_row.file_id);
    if (!encrypted.has_value()) {
        return false;
    }

    const auto& from_id    = dir_row.actor_id;
    const auto& main_actor = this->node->account_controller()->currentProfile().main()->get();

    auto from_actor_result = this->node->actor_index()->read_actor(from_id);
    if (!from_actor_result.has_value()) {
        return false;
    }
    auto from_actor = from_actor_result.value();

    auto content = main_actor.key().decrypt(encrypted.value(), from_actor.key().public_key());
    if (!content.has_value()) {
        return false;
    }
    auto chat_invite = Json::deserialize<Chat::ChatInvite>(content.value());
    if (!chat_invite.has_value()) {
        return false;
    }

    auto chat =
        Chat::Chat { .id       = "",
                     .owner_id = chat_invite->owner_id,
                     .file_id  = chat_invite->file_id,
                     .chat = Chat::ChatData { .chat_type = chat_invite->chat_type, .peer_id = dir_row.actor_id },
                     .chat_key = chat_invite->chat_key };

    // TODO: check if myself == myself? if i have ~ devices

    auto mychats_insert_result = this->insert_chat_to_mychats(chat);
    if (!mychats_insert_result.has_value()) {
        return false;
    }

    this->node->dfs()->remove_stored_file(owner_id, dir_row.file_id);

    add_new_message_joined(chat.owner_id, chat.file_id, main_actor.id());
    return true;
}
