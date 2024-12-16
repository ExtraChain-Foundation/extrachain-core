#pragma once

#include "blockchain/actor_id.h"
#include "encryption/encryption_tools.h"

namespace Chat {
    struct Chat {
        ActorId                myself;
        std::optional<ActorId> another;
        KeyBytes               chat_key;

        ActorId     file_actor_id;
        std::string file_id;
    };
    BOOST_DESCRIBE_STRUCT(Chat, (), (myself, another, chat_key, file_actor_id, file_id))
} // namespace Chat
