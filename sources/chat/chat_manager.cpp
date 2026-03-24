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

ChatManager::ChatManager(ExtraChainNode* node)
    : node(node) {
    QObject::connect(node->dfs(), &DfsController::downloaded, [this](ActorId owner_id, Dfs::DirRow dir_row) {
        if (this->chat_actor_ != owner_id) { // add my accounts?
            return;
        }

        auto my_chats = this->read_my_chats_row();
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
        auto main_id = this->node->account_controller()->current_profile().main_id();

        auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(this->node->dfs()->get_db_instance(),
                                                                        main_id); // TODO: add where / field
        if (!dir_rows.has_value()) {
            return;
        }

        for (const auto& dir_row : dir_rows.value()) {
            this->parse_invite(main_id, dir_row);
        }
    });

    QObject::connect(node->dfs(), &DfsController::downloaded, [this, node](ActorId owner_id, Dfs::DirRow dir_row) {
        for (const auto& chat : std::as_const(chats_)) {
            if (chat.owner_id != owner_id && chat.file_id != dir_row.file_id) {
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
                                     this->node->dfs()->read_vector_row(owner_id,
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
    const auto main_actor_id = node->account_controller()->current_profile().main_id();
    chat_actor_              = main_actor_id;

    // TODO: my actor = use actor for chats

    // ... check if chats is exists ...

    auto db_instance = node->dfs()->get_db_instance();
    auto rows        = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_instance, main_actor_id);
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
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(db_instance,
                                                                          network_id,
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

    auto custom = ThothCustom { .ignored = { chat->owner_id } };
    node->thoth_manager()->add_thoth_record(chat->owner_id, chat->file_id, Json::serialize(custom));

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::invite(const Chat::Chat& chat) {
    // check if with this person chat exists
    auto main_actor_id = node->account_controller()->current_profile().main_id();

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

std::expected<Chat::Chat, ChatError> ChatManager::create_channel(const std::string &name) {
    const auto main_actor_id = node->account_controller()->current_profile().main_id();
    chat_actor_              = main_actor_id;

    auto db_instance = node->dfs()->get_db_instance();
    auto rows        = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_instance, main_actor_id);
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

    auto channel_hash = node->dfs()->create_file_id_from(
        fmt::format("{}{}{}", name, Utils::current_date_ms(), main_actor_id.to_string())).substr(0, 10);
    auto channel_name = fmt::format("Channel-{}", channel_hash);

    auto chat     = Chat::Chat {};
    chat.chat_key = Cryptography::keygen();

    // Create channel vector (public)
    auto store_res = node->dfs()->store_vector(main_actor_id,
                                               main_actor_id,
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
    auto dict_res  = node->dfs()->store_dictionary(main_actor_id, chat_actor_, meta_name);
    if (dict_res.has_value() && !name.empty()) {
        node->dfs()->dictionary_set_value(main_actor_id, dict_res->file_id, "name", name, chat_actor_);
    }

    // TODO: public channels list — needs name field in vector entry
    // auto channels_status = node->create_channels_vector();
    // if (channels_status == DfsFileStatus::CantCreate) {
    //     return std::unexpected(ChatError::Unknown);
    // }
    //
    // const auto system_actor_id = node->account_controller()->system_actor().id();
    // auto channels_row =
    //     Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(db_instance,
    //                                                                       system_actor_id,
    //                                                                       Dfs::Basic::TEMPLATE_VECTOR,
    //                                                                       ExtraChainNode::CHANNELS_VECTOR_NAME);
    // if (!channels_row.has_value()) {
    //     return std::unexpected(ChatError::Unknown);
    // }
    //
    // auto channel_declared = node->dfs()->add_file_id(network_id,
    //                                                  channels_row->owner_id,
    //                                                  channels_row->file_id,
    //                                                  chat.owner_id,
    //                                                  chat.file_id,
    //                                                  main_actor_id,
    //                                                  0,
    //                                                  Dfs::FileIdState::Without);
    // if (!channel_declared.has_value()) {
    //     return std::unexpected(ChatError::Unknown);
    // }

    insert_chat_to_mychats(chat);
    add_new_message_created(chat.owner_id, chat.file_id);

    return chat;
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

    auto author_id = node->account_controller()->current_profile().main_id();
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
    return chat;
}

std::expected<std::vector<Chat::Chat>, ChatError> ChatManager::read_chats() {
    auto main_actor = node->account_controller()->current_profile().main()->get();
    auto my_chats   = this->read_my_chats_row();

    if (!my_chats.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_ };
    auto rows =
        node->dfs()->read_vector_rows(my_chats->actor_id, my_chats->file_id, "where status = '1'", security_actor);
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

        chat.value().chat_key = {};
        // eTemp("[ChatManager] Chat: {}", chat.value());
    }

    chats_ = chats;
    return chats;
}

std::expected<std::vector<Chat::Message>, ChatError> ChatManager::read_chat_messages(const ActorId&     owner_id,
                                                                                     const std::string& file_id,
                                                                                     bool               quick) {
    auto chat = this->get_chat(owner_id, file_id);
    if (!quick && !chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto security_key = quick ? Dfs::DataSecurityData() : Dfs::DataSecurityKey { .key = chat->chat_key };

    bool encryption = true;
    if (!quick && chat->chat.chat_type.has_value() && chat->chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
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

    auto security_key = Dfs::DataSecurityKey { .key = chat->chat_key };
    bool encryption   = true;
    if (chat->chat.chat_type.has_value() && chat->chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
    }

    constexpr int batch_size = 10;
    Chat::Message best;
    std::uint64_t best_ts = 0;
    bool found_non_edited = false;

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

    bool encryption = true;
    if (chat->chat.chat_type.has_value() && chat->chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
        if (chat->owner_id != chat_actor_) {
            return std::unexpected(ChatError::Unknown);
        }
    }

    auto security_key = Dfs::DataSecurityKey { .key = chat->chat_key };
    auto res          = node->dfs()->add_vector_row(owner_id,
                                           file_id,
                                           message,
                                           chat_actor_,
                                           encryption ? security_key : Dfs::DataSecurityData(),
                                           true);

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
    auto text = Utils::trim(Utils::sanitize_text(message_text.text));
    if (text.empty()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto message_data = Chat::MessageData { .data = text, .reply_id = message_text.reply_id };
    message_data.type = Chat::MessageType::Text;
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
    if (text.empty()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto chat = get_chat(owner_id, file_id);
    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    bool encryption = true;
    if (chat->chat.chat_type.has_value() && chat->chat.chat_type == Chat::ChatType::Channel) {
        encryption = false;
        if (chat->owner_id != chat_actor_) {
            return std::unexpected(ChatError::Unknown);
        }
    }

    // Read original message to preserve its timestamp
    auto messages = read_chat_messages(owner_id, file_id);
    if (!messages.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::uint64_t original_timestamp = 0;
    for (const auto &msg : messages.value()) {
        if (msg.id == message_id) {
            original_timestamp = msg.timestamp;
            if (msg.message.original_timestamp.has_value()) {
                original_timestamp = msg.message.original_timestamp.value();
            }
            break;
        }
    }

    auto message_data = Chat::MessageData { .type = Chat::MessageType::Text, .data = text, .original_timestamp = original_timestamp };
    auto message      = Chat::Message { .id = message_id, .message = message_data };

    auto security_key = Dfs::DataSecurityKey { .key = chat->chat_key };
    auto res          = node->dfs()->update_vector_row(owner_id,
                                              file_id,
                                              message,
                                              chat_actor_,
                                              encryption ? security_key : Dfs::DataSecurityData());

    if (!res) {
        return std::unexpected(ChatError::Unknown);
    }

    return res;
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
    auto my_chats_result = this->read_my_chats_row();
    if (my_chats_result.has_value()) {
        return my_chats_result.value();
    }

    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          CHAT_MY_CHATS);
    if (!search_result.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto main_actor_id  = node->account_controller()->current_profile().main_id();
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

std::expected<Dfs::DirRow, ChatError> ChatManager::read_my_chats_row() {
    static Dfs::DirRow my_chats;
    if (!my_chats.empty()) {
        return my_chats;
    }

    auto main_actor_id = node->account_controller()->current_profile().main_id();
    chat_actor_        = main_actor_id;
    auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(node->dfs()->get_db_instance(), main_actor_id);
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

        auto actor       = node->account_controller()->current_profile().main()->get();
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

    auto my_chats = this->read_my_chats_row();
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
        return false;
    }

    const auto& from_id    = dir_row.actor_id;
    const auto& main_actor = this->node->account_controller()->current_profile().main()->get();

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

    auto custom = ThothCustom { .ignored = { main_actor.id() } };
    node->thoth_manager()->add_thoth_record(chat.owner_id, chat.file_id, Json::serialize(custom));

    return true;
}
