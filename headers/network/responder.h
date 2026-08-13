/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <string>
#include <unordered_set>

#include "network/message_body.h"

class Responder;

class ResponseSender {
public:
    virtual ~ResponseSender() = default;

    virtual std::string send_response(const std::string& data_serialized,
                                      MessageType        type,
                                      SendMode           send_mode,
                                      MessageStatus      status,
                                      const Responder&   responder) = 0;
};

/**
 * Describes the peer or peers that must receive a response.
 *
 * The value has no Qt dependency. NetworkManager performs the final send.
 * This keeps message handlers independent from the Qt client facade.
 */
class Responder {
public:
    Responder(ResponseSender* sender = nullptr)
        : sender_(sender) {
    }

    Responder(const Responder&) = default;

    template <class T>
    std::string send_response(const T& data, MessageType type, SendMode send_mode, MessageStatus status) const {
        if (sender_ == nullptr) {
            return "";
        }

        return send_response_impl(MessagePack::serialize(data), type, send_mode, status);
    }

    [[nodiscard]] const std::string& message_id() const {
        return message_id_;
    }

    [[nodiscard]] const std::string& ip() const {
        return ip_;
    }

    [[nodiscard]] const std::unordered_set<std::string>& identifiers() const {
        return identifiers_;
    }

    [[nodiscard]] const NodeId& node_id() const {
        return node_id_;
    }

    [[nodiscard]] int luminance() const {
        return luminance_;
    }

    bool add_identifier(const std::string& identifier) {
        if (identifier.empty()) {
            return false;
        }
        return identifiers_.insert(identifier).second;
    }

    bool remove_identifier(const std::string& identifier) {
        if (identifier.empty()) {
            return false;
        }
        return identifiers_.erase(identifier) != 0;
    }

    void set_ip(const std::string& ip) {
        ip_ = ip;
    }

    void set_message_id(const std::string& message_id) {
        message_id_ = message_id;
    }

    void set_message_type(MessageType type) {
        message_type_ = type;
    }

    void set_node_id(const NodeId& node_id) {
        node_id_ = node_id;
    }

    void set_luminance(int luminance) {
        luminance_ = luminance;
    }

    [[nodiscard]] Responder with_new_message_id() const {
        Responder responder   = *this;
        responder.message_id_ = generate_message_id();
        return responder;
    }

    [[nodiscard]] bool empty() const {
        return identifiers_.empty() && message_id_.empty();
    }

    Responder& operator=(const Responder&) = default;

private:
    std::string send_response_impl(const std::string& data_serialized,
                                   MessageType        type,
                                   SendMode           send_mode,
                                   MessageStatus      status) const;

    MessageType                     message_type_ = MessageType::Unknown;
    std::string                     ip_;
    std::unordered_set<std::string> identifiers_;
    std::string                     message_id_;
    NodeId                          node_id_;
    int                             luminance_ = 0;
    ResponseSender*                 sender_    = nullptr;
};
