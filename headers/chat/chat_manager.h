#pragma once

#include <extrachain_global.h>
#include <expected>
#include "blockchain/actor_id.h"
#include "chat/chat.h"
#include "chat/message.h"
#include "dfs/collection_template.h"
#include "dfs/dfs_utils.h"
#include "dfs/historical_collection.h"

static const std::string CHAT_DAPP_FOLDER        = ":DApp:Chat";
static const std::string CHAT_DAPP_INVITE_FOLDER = ":DApp:Chat:Invite";

class ExtraChainNode;

enum class ChatError {
    Unknown
};

class EXTRACHAIN_EXPORT ChatManager {
private:
    ExtraChainNode *node;

public:
    ChatManager(ExtraChainNode *node);

    std::expected<Chat::Chat, ChatError> create_chat(bool save_chat = true);
    std::expected<Chat::Chat, ChatError> create_dialogue(ActorId with);
    std::expected<Chat::Chat, ChatError> invite(const Chat::Chat &chat);

    std::expected<std::vector<Chat::Chat>, ChatError>    get_chats();
    std::expected<std::vector<Chat::Message>, ChatError> get_chat_messages(const ActorId     &actor_id,
                                                                           const std::string &file_id);

    std::expected<HistoricalCollectionRow, ChatError> add_new_message(const ActorId       &file_actor_id,
                                                                      const std::string   &file_id,
                                                                      const Chat::Message &message);

    static Dfs::CollectionTemplate &chats_template();
    static Dfs::CollectionTemplate &chat_template();

private:
    std::expected<Dfs::DirRow, ChatError>             create_mychats();
    std::expected<Dfs::DirRow, ChatError>             get_my_chats();
    std::expected<HistoricalCollectionRow, ChatError> insert_chat_to_mychats(const Chat::Chat &chat);

    ActorId chat_actor_;
};
