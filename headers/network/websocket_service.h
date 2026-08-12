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

#include "network/isocket_service.h"

#include <chrono>
#include <condition_variable>
#include <expected>
#include <memory>
#include <optional>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/stream.hpp>

#include "extrachain_global.h"

namespace ExtraChain::Core {
    class NetworkRuntime;
}

class EXTRACHAIN_EXPORT WebSocketService final : public SocketService {
public:
    using Tcp           = boost::asio::ip::tcp;
    using WebSocket     = boost::beast::websocket::stream<boost::beast::tcp_stream>;
    using Service       = std::shared_ptr<WebSocketService>;
    using ConnectResult = std::expected<Service, std::string>;

    static boost::asio::awaitable<ConnectResult> connect(ExtraChain::Core::NetworkRuntime& runtime,
                                                         std::string                       host,
                                                         std::uint16_t                     port,
                                                         PeerContext&                      context,
                                                         bool                              is_constant = false,
                                                         bool                              is_light    = false);
    static Service                               from_accepted(ExtraChain::Core::NetworkRuntime& runtime,
                                                               Tcp::socket                       socket,
                                                               PeerContext&                      context);

    ~WebSocketService() override;

    boost::asio::awaitable<void> run(bool accepted_socket);

    [[nodiscard]] bool              is_active() const override;
    [[nodiscard]] std::string       protocol_string() const override;
    [[nodiscard]] Network::Protocol protocol() const override;
    std::uint16_t                   port() const override;
    std::uint16_t                   server_port() const override;
    void send_message(std::span<const std::uint8_t> data, Priority priority = Priority::High) override;
    void flush() override;
    void close_connection() override;
    [[nodiscard]] bool         wait_closed(std::chrono::milliseconds timeout) override;
    [[nodiscard]] std::int64_t pending_bytes() const noexcept override;

private:
    explicit WebSocketService(ExtraChain::Core::NetworkRuntime& runtime, PeerContext& context);

    boost::asio::awaitable<std::expected<void, std::string>> open(std::string host, std::uint16_t port);
    boost::asio::awaitable<void>                             run_on_strand(bool accepted_socket);
    boost::asio::awaitable<bool>                             exchange_keys();
    boost::asio::awaitable<bool>                             exchange_handshake();
    boost::asio::awaitable<void>                             read_loop();
    boost::asio::awaitable<void>                             write_loop();
    boost::asio::awaitable<bool>                             write_text(std::string_view text);
    boost::asio::awaitable<std::optional<std::string>>       read_text();
    boost::asio::awaitable<Data>                             prepare_send_async(Data message);
    boost::asio::awaitable<Data>                             prepare_receive_async(Data message);
    boost::asio::awaitable<void>                             process_binary(std::vector<std::uint8_t> message);
    void report_error(Network::SocketServiceError code, std::string detail = {});
    void finish_close();
    void operation_finished();

    boost::asio::strand<boost::asio::any_io_executor> strand_;
    ExtraChain::Core::NetworkRuntime&                 runtime_;
    std::unique_ptr<WebSocket>                        websocket_;
    boost::asio::steady_timer                         queue_signal_;
    std::atomic_bool                                  running_ { false };
    std::atomic_bool                                  write_running_ { false };
    std::atomic<std::int64_t>                         socket_pending_bytes_ { 0 };
    std::mutex                                        close_mutex_;
    std::condition_variable                           close_condition_;
    std::atomic_bool                                  transport_closed_ { false };
    std::atomic_uint                                  active_operations_ { 0 };
    std::chrono::seconds                              operation_timeout_ { 10 };
};
