#pragma once

#include "blockchain/actor_id.h"
// #include "encryption/encryption_tools.h"

namespace Chat {
    struct Message {
        // id
        // timestamp
        ActorId     sender;
        std::string message;
    };
    BOOST_DESCRIBE_STRUCT(Message, (), (sender, message))
} // namespace Chat
