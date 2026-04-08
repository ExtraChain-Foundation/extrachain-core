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

#include "network/websocket_service.h"

#include "network/network_manager.h"

WebSocketService::WebSocketService(asio::io_context &ioc, ExtraChainNode *node)
    : SocketService(node)
    , ioc_(ioc)
    , strand_(asio::make_strand(ioc))
    , write_timer_(std::make_unique<asio::steady_timer>(strand_)) {
}

WebSocketService::~WebSocketService() {
    eLog("[WS] Destroyed: {}", ip_);
}

Task<SocketService::Ptr> WebSocketService::connect(asio::io_context &ioc,
                                                   const std::string &host,
                                                   uint16_t port,
                                                   ExtraChainNode *node,
                                                   bool is_constant,
                                                   bool is_light) {
    auto service = std::shared_ptr<WebSocketService>(new WebSocketService(ioc, node));
    service->set_constant(is_constant);
    if (is_light) {
        service->mode_ = SocketMode::Light;
    }
    service->ip_        = host;
    service->timestamp_ = Utils::current_date_ms();

    co_await service->do_connect(host, port);
    co_return service;
}

SocketService::Ptr WebSocketService::from_accepted(tcp::socket socket, asio::io_context &ioc, ExtraChainNode *node) {
    auto service        = std::shared_ptr<WebSocketService>(new WebSocketService(ioc, node));
    service->timestamp_ = Utils::current_date_ms();

    beast::error_code ec;
    auto endpoint = socket.remote_endpoint(ec);
    if (!ec) {
        service->ip_   = endpoint.address().to_string();
        service->port_ = endpoint.port();
    }

    service->ws_ = std::make_unique<WebSocket>(std::move(socket));
    return service;
}

VoidTask WebSocketService::do_connect(const std::string &host, uint16_t port) {
    tcp::resolver resolver(ioc_);

    auto [ec_resolve, results] = co_await resolver.async_resolve(
        host,
        std::to_string(port),
        asio::as_tuple(asio::use_awaitable));

    if (ec_resolve) {
        eLog("[WS] Resolve failed: {}", ec_resolve.message());
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::Unknown, ec_resolve.message(), identifier_);
        }
        co_return;
    }

    tcp::socket socket(ioc_);
    auto [ec_connect, endpoint] = co_await asio::async_connect(
        socket,
        results,
        asio::as_tuple(asio::use_awaitable));

    if (ec_connect) {
        eLog("[WS] Connect failed: {}", ec_connect.message());
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::Unknown, ec_connect.message(), identifier_);
        }
        co_return;
    }

    port_ = endpoint.port();
    ws_ = std::make_unique<WebSocket>(std::move(socket));
    ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

    auto [ec_handshake] = co_await ws_->async_handshake(
        host,
        "/",
        asio::as_tuple(asio::use_awaitable));

    if (ec_handshake) {
        eLog("[WS] WebSocket handshake failed: {}", ec_handshake.message());
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectHandshake, ec_handshake.message(), identifier_);
        }
        co_return;
    }
}

VoidTask WebSocketService::run(bool accepted_socket) {
    if (!ws_) {
        co_return;
    }

    running_ = true;

    try {
        if (accepted_socket) {
            ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
            auto [ec_accept] = co_await ws_->async_accept(asio::as_tuple(asio::use_awaitable));
            if (ec_accept) {
                eLog("[WS] Accept failed: {}", ec_accept.message());
                if (on_error) {
                    on_error(shared_from_this(), Network::SocketServiceError::Unknown, ec_accept.message(), identifier_);
                }
                co_return;
            }
        }

        co_await do_key_exchange();
        if (!running_) {
            co_return;
        }

        co_await do_handshake();
        if (!running_) {
            co_return;
        }

        asio::co_spawn(strand_, [self = std::static_pointer_cast<WebSocketService>(shared_from_this())]() -> VoidTask {
            co_await self->read_loop();
        }, asio::detached);

        asio::co_spawn(strand_, [self = std::static_pointer_cast<WebSocketService>(shared_from_this())]() -> VoidTask {
            co_await self->write_loop();
        }, asio::detached);
    } catch (const std::exception &e) {
        eLog("[WS] Run exception: {}", e.what());
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::Unknown, e.what(), identifier_);
        }
    }
}

VoidTask WebSocketService::do_key_exchange() {
    const auto pub_key_str = Utils::to_base64(ByteArray(priv_.public_key()).toString());

    ws_->text(true);
    auto [ec_write, bytes_written] = co_await ws_->async_write(
        asio::buffer(pub_key_str),
        asio::as_tuple(asio::use_awaitable));
    (void)bytes_written;

    if (ec_write) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectPublicKey, "", identifier_);
        }
        co_return;
    }

    beast::flat_buffer buffer;
    auto [ec_read, bytes] = co_await ws_->async_read(
        buffer,
        asio::as_tuple(asio::use_awaitable));
    (void)bytes;

    if (ec_read) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectPublicKey, "", identifier_);
        }
        co_return;
    }

    const auto received = beast::buffers_to_string(buffer.data());
    auto pub_result = Utils::from_base64(received);
    if (!pub_result.has_value()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectPublicKey, "", identifier_);
        }
        co_return;
    }

    pub_ = KeyPublic(ByteArray(pub_result.value()).toArray<crypto_sign_PUBLICKEYBYTES>());
    is_pub_ = true;
}

VoidTask WebSocketService::do_handshake() {
    const auto first_message = generate_first_message();
    const auto encrypted = prepare_send_message(first_message);
    if (encrypted.empty()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectHandshake, "", identifier_);
        }
        co_return;
    }

    const auto encoded = Utils::to_base64(std::string(encrypted.begin(), encrypted.end()));

    ws_->text(true);
    auto [ec_write, bytes_written] = co_await ws_->async_write(
        asio::buffer(encoded),
        asio::as_tuple(asio::use_awaitable));
    (void)bytes_written;

    if (ec_write) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectHandshake, "", identifier_);
        }
        co_return;
    }

    beast::flat_buffer buffer;
    auto [ec_read, bytes] = co_await ws_->async_read(
        buffer,
        asio::as_tuple(asio::use_awaitable));
    (void)bytes;

    if (ec_read) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectFirstMessage, "", identifier_);
        }
        co_return;
    }

    const auto received = beast::buffers_to_string(buffer.data());
    auto decoded_opt = Utils::from_base64(received);
    if (!decoded_opt.has_value()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectFirstMessage, "", identifier_);
        }
        co_return;
    }

    auto decrypted = prepare_receive_message(std::vector<uint8_t>(decoded_opt->begin(), decoded_opt->end()));
    if (decrypted.empty()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectFirstMessage, "", identifier_);
        }
        co_return;
    }

    const std::string decrypted_str(decrypted.begin(), decrypted.end());
    if (decrypted_str.starts_with("Error ")) {
        close_connection();
        co_return;
    }

    auto handshake_result = Json::deserialize<HandshakeMessage>(decrypted_str);
    if (!handshake_result.has_value()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectFirstMessage, "", identifier_);
        }
        co_return;
    }

    if (check_first_message(handshake_result.value())) {
        for (const auto &cached : message_cache_) {
            co_await process_binary_message(cached);
        }
        message_cache_.clear();
    }
}

VoidTask WebSocketService::read_loop() {
    while (running_ && ws_ && ws_->is_open()) {
        beast::flat_buffer buffer;
        auto [ec, bytes] = co_await ws_->async_read(
            buffer,
            asio::as_tuple(asio::use_awaitable));
        (void)bytes;

        if (ec) {
            if (ec != websocket::error::closed) {
                eLog("[WS] Read error: {}", ec.message());
            }
            break;
        }

        if (ws_->got_text()) {
            co_await process_text_message(beast::buffers_to_string(buffer.data()));
        } else {
            std::vector<uint8_t> data(
                asio::buffers_begin(buffer.data()),
                asio::buffers_end(buffer.data()));
            co_await process_binary_message(data);
        }
    }

    close_connection();
}

VoidTask WebSocketService::write_loop() {
    while (running_ && ws_ && ws_->is_open()) {
        std::vector<uint8_t> data;

        if (!high_queue_.empty()) {
            data = std::move(high_queue_.front());
            high_queue_.pop_front();
        } else if (!normal_queue_.empty()) {
            data = std::move(normal_queue_.front());
            normal_queue_.pop_front();
        } else if (!low_queue_.empty()) {
            data = std::move(low_queue_.front());
            low_queue_.pop_front();
        }

        if (!data.empty()) {
            const auto encrypted = prepare_send_message(data);
            if (encrypted.empty()) {
                continue;
            }

            ws_->binary(true);
            auto [ec, bytes_written] = co_await ws_->async_write(
                asio::buffer(encrypted),
                asio::as_tuple(asio::use_awaitable));
            (void)bytes_written;

            if (ec) {
                eLog("[WS] Write error: {}", ec.message());
                break;
            }
        } else {
            write_timer_->expires_at(asio::steady_timer::time_point::max());
            auto [ec] = co_await write_timer_->async_wait(asio::as_tuple(asio::use_awaitable));
            (void)ec;
        }
    }
}

VoidTask WebSocketService::process_text_message(const std::string &message) {
    if (!message.empty()) {
        eLog("[WS] Unexpected text message after activation: {}", message.substr(0, 64));
    }
    co_return;
}

VoidTask WebSocketService::process_binary_message(const std::vector<uint8_t> &data) {
    if (!activated_) {
        message_cache_.push_back(data);
        co_return;
    }

    const auto decrypted = prepare_receive_message(data);
    if (decrypted.empty()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::EmptyMessage, "", identifier_);
        }
        co_return;
    }

    auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    std::string message(decrypted.begin(), decrypted.end());
    std::string ip = ip_;
    std::string identifier = identifier_;

    asio::post(ioc_, [self, message = std::move(message), ip = std::move(ip), identifier = std::move(identifier)]() mutable {
        self->node_->network()->message_received(message, ip, identifier);
    });
}

bool WebSocketService::is_active() const {
    return activated_ && ws_ && ws_->is_open();
}

std::string WebSocketService::protocol_string() const {
    return "WebSocket";
}

Network::Protocol WebSocketService::protocol() const {
    return Network::Protocol::WebSocket;
}

uint16_t WebSocketService::port() const {
    if (!ws_) {
        return 0;
    }

    beast::error_code ec;
    auto endpoint = ws_->next_layer().remote_endpoint(ec);
    if (ec) {
        return port_;
    }

    return endpoint.port();
}

uint16_t WebSocketService::server_port() const {
    return node_->network()->ws_port;
}

void WebSocketService::send_message(const std::vector<uint8_t> &data, Priority priority) {
    if (!is_active() || closed_) {
        return;
    }

    if (data.empty()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncorrectMessage, "", identifier_);
        }
        return;
    }

    auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    asio::post(strand_, [self, data, priority]() {
        switch (priority) {
        case Priority::High:
            self->high_queue_.push_back(data);
            break;
        case Priority::Normal:
            self->normal_queue_.push_back(data);
            break;
        case Priority::Low:
            self->low_queue_.push_back(data);
            break;
        }

        self->write_timer_->cancel();
    });
}

void WebSocketService::flush() {
}

int64_t WebSocketService::pending_bytes() const {
    if (!ws_) {
        return 0;
    }

    return bytes_outgoing_;
}

void WebSocketService::close_connection() {
    if (closed_async_.exchange(true)) {
        return;
    }

    closed_ = true;
    running_ = false;
    activated_ = false;

    high_queue_.clear();
    normal_queue_.clear();
    low_queue_.clear();
    message_cache_.clear();

    if (write_timer_) {
        write_timer_->cancel();
    }

    if (ws_ && ws_->is_open()) {
        beast::error_code ec;
        ws_->close(websocket::close_code::normal, ec);
        if (ec && ec != websocket::error::closed) {
            eLog("[WS] Close error: {}", ec.message());
        }
    }

    SocketService::close_connection();
}
