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

#include "managers/extrachain_node.h"
#include "network/isocket_service.h"
#include "utils/exc_utils.h"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include <deque>

#include "extrachain_global.h"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

template<typename T>
using Task = asio::awaitable<T>;
using VoidTask = Task<void>;

class EXTRACHAIN_EXPORT WebSocketService : public SocketService {
public:
    using WebSocket = websocket::stream<tcp::socket>;

    static Task<Ptr> connect(asio::io_context &ioc,
                             const std::string &host,
                             uint16_t port,
                             ExtraChainNode *node,
                             bool is_constant = false,
                             bool is_light = false);

    static Ptr from_accepted(tcp::socket socket, asio::io_context &ioc, ExtraChainNode *node);

    ~WebSocketService() override;

    VoidTask run(bool accepted_socket = false);

    bool              is_active() const override;
    std::string       protocol_string() const override;
    Network::Protocol protocol() const override;
    uint16_t          port() const override;
    uint16_t          server_port() const override;
    void              send_message(const std::vector<uint8_t> &data, Priority priority) override;
    void              flush() override;
    void              close_connection() override;
    int64_t           pending_bytes() const override;

private:
    explicit WebSocketService(asio::io_context &ioc, ExtraChainNode *node);

    VoidTask do_connect(const std::string &host, uint16_t port);
    VoidTask do_key_exchange();
    VoidTask do_handshake();
    VoidTask read_loop();
    VoidTask write_loop();
    VoidTask process_text_message(const std::string &message);
    VoidTask process_binary_message(const std::vector<uint8_t> &data);

    asio::io_context &ioc_;
    asio::strand<asio::io_context::executor_type> strand_;
    std::unique_ptr<WebSocket> ws_;
    std::unique_ptr<asio::steady_timer> write_timer_;
    std::deque<std::vector<uint8_t>> high_queue_;
    std::deque<std::vector<uint8_t>> normal_queue_;
    std::deque<std::vector<uint8_t>> low_queue_;
    std::vector<std::vector<uint8_t>> message_cache_;
    std::atomic<bool> running_ { false };
    std::atomic<bool> closed_async_ { false };
};
