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

#include "chat/secret_payload.h"

#include <functional>
#include <memory>
#include <optional>

namespace SecretChat {

// A chat is named by the identity key of the other side
using ChatId = Key32;

// How envelopes leave the device. The engine hands over a mailbox id and the wire bytes of one
// message; getting them to the network is the job of whoever implements this.
class Transport {
public:
    virtual ~Transport()                                            = default;
    virtual void send(const MailboxId &id, const Bytes &wire) = 0;
};

struct Event {
    enum class Kind {
        NewChat,    // a first message from a new person arrived; Message follows
        Message,    // text arrived
        Delivered,  // the other side has the message: one tick
        Read,       // the other side has read it: two ticks
        Reaction,   // a reaction to a message was set or taken back
        PeerOnline, // the other side said it is here
        Failed,     // out of attempts, still queued and sent again when the other side shows up
    };

    Kind          kind = Kind::Message;
    ChatId        chat {};
    MessageId     mid {};      // Message, Delivered, Read, Failed, Reaction (the id of the reaction)
    MessageId     target {};   // Reaction: the message reacted to
    MessageId     reply_to {}; // Message
    std::string   text;        // Message
    std::string   emoji;       // Reaction
    bool          removed = false;
    std::uint64_t sent_at = 0;
};

struct EngineConfig {
    IdentityKeys              identity;
    // Signed prekeys others may start a session with; the newest published, older ones kept
    // until their first messages stop arriving
    std::vector<SignedPreKey> prekeys;
    Transport                *transport = nullptr;
    std::function<void(const Event &)>                on_event;
    // Called after every change of a chat; the blob is all that is needed to restore it
    std::function<void(const ChatId &, const Bytes &)> save_chat;

    // Pauses before each further attempt; the last one repeats
    std::vector<std::uint64_t> retry_ms { 10'000, 60'000, 300'000 };
    std::uint32_t              max_attempts      = 8;
    // After the attempts are used up the message is reported as failed, and it is still tried
    // this often until it gets through or the chat is removed
    std::uint64_t              failed_retry_ms   = 900'000;
    // Tell each contact that this device is online whenever it connects
    bool announce_presence = true;
};

// The whole logic of secret chats above one session: start and answer chats, queue what could
// not be delivered, acknowledge, resend, react. No network, no clock and no storage of its own:
// time comes in as arguments (milliseconds), envelopes as calls, everything else goes out
// through the configured callbacks. Not thread safe: call it from one thread.
class Engine {
public:
    explicit Engine(EngineConfig config);
    ~Engine();
    Engine(const Engine &)            = delete;
    Engine &operator=(const Engine &) = delete;

    std::expected<ChatId, Error>    start_chat(const PreKeyBundle &peer, std::string first_text, std::uint64_t now,
                                               MessageId *first_mid = nullptr);
    std::expected<MessageId, Error> send_text(const ChatId &chat, std::string text, std::uint64_t now,
                                              const MessageId &reply_to = {});
    std::expected<MessageId, Error> send_reaction(const ChatId &chat, const MessageId &target, std::string emoji,
                                                  bool remove, std::uint64_t now);
    std::expected<void, Error>      send_read(const ChatId &chat, std::vector<MessageId> mids, std::uint64_t now);

    // An envelope arrived from the network under the given mailbox id
    void on_envelope(const MailboxId &id, std::span<const std::uint8_t> wire, std::uint64_t now);
    // The device connected to the network
    void on_connected(std::uint64_t now);
    // Call about once a second: resends what is due
    void tick(std::uint64_t now);

    std::expected<void, Error> restore_chat(std::span<const std::uint8_t> blob);
    void                       remove_chat(const ChatId &chat);

    std::vector<ChatId>        chats() const;
    std::optional<std::string> safety_number(const ChatId &chat) const;

    struct Pending {
        MessageId     mid {};
        std::uint32_t attempts = 0;
        bool          failed   = false;
    };
    std::vector<Pending> pending(const ChatId &chat) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace SecretChat
