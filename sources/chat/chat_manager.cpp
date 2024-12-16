#include "chat/chat_manager.h"

#include "chat/chat.h"
#include "dfs/dfs_controller.h"
#include "encryption/encryption_tools.h"
#include "managers/extrachain_node.h"

ChatManager::ChatManager(ExtraChainNode* node)
    : node(node) {
    QObject::connect(node->dfs(), &DfsController::downloaded, [this](Dfs::DirRow dir_row) {
        if (dir_row.folder == CHAT_DAPP_INVITE_FOLDER) {
            auto content = Dfs::Tables::ActorDirFile::get_file_content(dir_row.actor_id, dir_row.file_id);
            if (!content.has_value()) {
                return;
            }

            auto chat = Json::deserialize<Chat::Chat>(content.value());
            if (!chat.has_value()) {
                return;
            }

            if (!chat->another.has_value()) {
                return;
            }

            ActorId temp  = chat->another.value();
            chat->another = chat->myself;
            chat->myself  = temp;

            this->insert_chat_to_mychats(chat.value());
        }

        // if MyChats downloaded
    });

    QObject::connect(node->dfs(),
                     &DfsController::collectionChange,
                     [this](Dfs::DirRow dir_row, HistoricalCollectionRow row) {
                         if (dir_row.name == chats_template().name()) {
                             // update chat to ui
                         }
                     });
}

EXTRACHAIN_EXPORT std::expected<Chat::Chat, ChatError> ChatManager::create_chat(bool save_chat) {
    KeyBytes key        = Cryptography::keygen();
    auto     main_actor = node->accountController()->mainActor();
    chat_actor_         = node->accountController()->mainActor()->id();

    // TODO: my actor = use actor for chats

    // ... check if chats is exists ...

    auto rows = Dfs::Tables::ActorDirFile::get_dir_rows(main_actor->id());
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    // TODO: is_my_chats_exists -> create else return error Exists -> load chat in gui

    auto chat = Chat::Chat { .myself = chat_actor_, .chat_key = key };

    auto store_chat_res =
        node->dfs()->store_collection(chat_actor_, fmt::format("chat-{}", chat.another), chat_template());
    if (!store_chat_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    chat.file_actor_id = store_chat_res->actor_id;
    chat.file_id       = store_chat_res->file_id;

    if (save_chat) {
        insert_chat_to_mychats(chat);
    }

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::create_dialogue(ActorId with) {
    auto chat = create_chat(false);

    if (!chat.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    chat->another = with.to_string();
    insert_chat_to_mychats(chat.value());
    invite(chat.value());

    return chat;
}

std::expected<Chat::Chat, ChatError> ChatManager::invite(const Chat::Chat& chat) {
    // check if with this person chat exists
    auto main_actor = node->accountController()->mainActor();

    if (!chat.another.has_value()) {
        return chat;
    }

    auto json = Json::serialize(chat);

    auto res = node->dfs()->store_data_as_file(chat.another.value(),
                                               ByteArray(json).toBytes(),
                                               CHAT_DAPP_INVITE_FOLDER,
                                               fmt::format("ChatFrom_{}", main_actor->id()),
                                               Dfs::SecurityLevel::Public);

    if (!res.has_value()) {
        eFatal("Invite error");
        return std::unexpected(ChatError::Unknown);
    }

    return chat;
}

std::expected<std::vector<Chat::Chat>, ChatError> ChatManager::get_chats() {
    auto main_actor = node->accountController()->mainActor();
    auto my_chats   = get_my_chats();

    if (!my_chats.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto chain = HistoricalCollection::load(main_actor, my_chats->actor_id, my_chats->file_id);
    if (!chain.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    auto rows = chain->get_collection_rows();
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::vector<Chat::Chat> chats;
    chats.reserve(rows->size());

    for (const auto& row : rows.value()) {
        auto chat = Utils::from_dbrow<Chat::Chat>(row);
        if (!chat.has_value()) {
            continue;
        }
        chats.push_back(chat.value());
    }

    return chats;
}

std::expected<std::vector<Chat::Message>, ChatError> ChatManager::get_chat_messages(const ActorId&     actor_id,
                                                                                    const std::string& file_id) {
    auto db_rows = node->dfs()->get_collection_rows(actor_id, file_id);

    if (!db_rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    std::vector<Chat::Message> messages;
    messages.reserve(db_rows->size());

    for (const auto& db_row : db_rows.value()) {
        auto message = Utils::from_dbrow<Chat::Message>(db_row);
        if (!message.has_value()) {
            continue;
        }
        messages.push_back(message.value());
    }

    return messages;
}

std::expected<HistoricalCollectionRow, ChatError> ChatManager::add_new_message(const ActorId&       file_actor_id,
                                                                               const std::string&   file_id,
                                                                               const Chat::Message& message) {
    // ... checks for file ...

    auto res = node->dfs()->add_collection_row(file_actor_id, file_id, message);
    if (!res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    return res.value().second;
}

Dfs::CollectionTemplate& ChatManager::chats_template() {
    static auto my_chats_template = Dfs::CollectionTemplate::create("MyChats").value().add_fields(
        { Dfs::Field::ActorId("myself").not_null(),        // .unique(),
          Dfs::Field::ActorId("another"),                  // .unique(),
          Dfs::Field::ActorId("file_actor_id").not_null(), //.unique(),
          Dfs::Field::String("file_id").not_null(),        // .unique(),
          Dfs::Field::String("chat_key").not_null() });
    return my_chats_template;
}

Dfs::CollectionTemplate& ChatManager::chat_template() {
    static auto chat_template = Dfs::CollectionTemplate::create("Chat").value().add_fields(
        { Dfs::Field::ActorId("sender").not_null(), Dfs::Field::String("message").not_null() });
    return chat_template;
}

std::expected<Dfs::DirRow, ChatError> ChatManager::create_mychats() {
    auto my_chats_result = get_my_chats();
    if (my_chats_result.has_value()) {
        return my_chats_result.value();
    }

    auto main_actor = node->accountController()->mainActor();
    auto store_chats_res =
        node->dfs()->store_collection(main_actor->id(), chats_template().name(), chats_template());
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

    auto main_actor = node->accountController()->mainActor();
    chat_actor_     = main_actor->id();
    auto rows       = Dfs::Tables::ActorDirFile::get_dir_rows(main_actor->id());
    if (!rows.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    for (const auto& row : rows.value()) {
        if (row.name == chats_template().name()) { // TODO: need normal search
            my_chats = row;
        }
    }

    if (my_chats.empty()) {
        return std::unexpected(ChatError::Unknown);
    }

    return my_chats;
}

std::expected<HistoricalCollectionRow, ChatError> ChatManager::insert_chat_to_mychats(const Chat::Chat& chat) {
    auto my_chats = get_my_chats();
    if (!my_chats.has_value()) {
        auto my_chats_result = create_mychats();
        if (!my_chats_result.has_value()) {
            return std::unexpected(ChatError::Unknown);
        }
        my_chats = my_chats_result;
    }

    auto res = node->dfs()->add_collection_row(chat_actor_, my_chats->file_id, chat);

    if (!res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    return res.value().second;
}
