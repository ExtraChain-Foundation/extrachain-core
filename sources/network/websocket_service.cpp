/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "network/websocket_service.h"

#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/stream_traits.hpp>
#include <boost/beast/websocket/error.hpp>
#include <boost/beast/websocket/stream_base.hpp>

#include "network/network_runtime.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils_base64.h"
#include "utils/serialization.h"

namespace asio      = boost::asio;
namespace beast     = boost::beast;
namespace websocket = beast::websocket;

namespace {
    constexpr std::size_t MAX_INBOUND_MESSAGE_BYTES    = 72 * 1024 * 1024;
    constexpr std::size_t ASYNC_CRYPTO_THRESHOLD_BYTES = 64 * 1024;

    std::uint64_t current_time_ms() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count());
    }
} // namespace

WebSocketService::WebSocketService(ExtraChain::Core::NetworkRuntime& runtime, PeerContext& context)
    : SocketService(context)
    , strand_(asio::make_strand(runtime.executor()))
    , runtime_(runtime)
    , queue_signal_(strand_) {
}

WebSocketService::~WebSocketService() {
    eLog("[WS] Destroyed: {}", ip_);
}

asio::awaitable<WebSocketService::ConnectResult> WebSocketService::connect(
    ExtraChain::Core::NetworkRuntime& runtime,
    std::string                       host,
    std::uint16_t                     port,
    PeerContext&                      context,
    bool                              is_constant,
    bool                              is_light) {
    auto service = std::shared_ptr<WebSocketService>(new WebSocketService(runtime, context));
    service->set_constant(is_constant);
    service->mode_      = is_light ? SocketMode::Light : SocketMode::Full;
    service->ip_        = host;
    service->timestamp_ = current_time_ms();

    auto opened =
        co_await asio::co_spawn(service->strand_, service->open(std::move(host), port), asio::use_awaitable);
    if (!opened.has_value()) {
        co_return std::unexpected(std::move(opened.error()));
    }
    co_return service;
}

WebSocketService::Service WebSocketService::from_accepted(ExtraChain::Core::NetworkRuntime& runtime,
                                                          Tcp::socket                       socket,
                                                          PeerContext&                      context) {
    auto service        = std::shared_ptr<WebSocketService>(new WebSocketService(runtime, context));
    service->timestamp_ = current_time_ms();

    boost::system::error_code error;
    const auto                endpoint = socket.remote_endpoint(error);
    if (!error) {
        service->ip_   = endpoint.address().to_string();
        service->port_ = endpoint.port();
    }
    service->websocket_ = std::make_unique<WebSocket>(std::move(socket));
    return service;
}

asio::awaitable<std::expected<void, std::string>> WebSocketService::open(std::string host, std::uint16_t port) {
    try {
        Tcp::resolver             resolver(strand_);
        boost::system::error_code error;
        const auto                endpoints = co_await resolver.async_resolve(host,
                                                               std::to_string(port),
                                                               asio::redirect_error(asio::use_awaitable, error));
        if (error) {
            co_return std::unexpected("resolve: " + error.message());
        }

        websocket_   = std::make_unique<WebSocket>(strand_);
        auto& stream = beast::get_lowest_layer(*websocket_);
        stream.expires_after(operation_timeout_);
        const auto endpoint =
            co_await stream.async_connect(endpoints, asio::redirect_error(asio::use_awaitable, error));
        if (error) {
            co_return std::unexpected("connect: " + error.message());
        }

        port_ = endpoint.port();
        websocket_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        websocket_->read_message_max(MAX_INBOUND_MESSAGE_BYTES);
        stream.expires_never();
        co_await websocket_->async_handshake(host, "/", asio::redirect_error(asio::use_awaitable, error));
        if (error) {
            co_return std::unexpected("websocket handshake: " + error.message());
        }
    } catch (const std::exception& exception) {
        co_return std::unexpected(exception.what());
    }

    co_return std::expected<void, std::string> {};
}

asio::awaitable<void> WebSocketService::run(bool accepted_socket) {
    const auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    co_await asio::co_spawn(
        strand_,
        [self, accepted_socket]() -> asio::awaitable<void> {
            co_await self->run_on_strand(accepted_socket);
        },
        asio::use_awaitable);
}

asio::awaitable<void> WebSocketService::run_on_strand(bool accepted_socket) {
    active_operations_.fetch_add(1, std::memory_order_acq_rel);
    struct OperationGuard final {
        WebSocketService* service;
        ~OperationGuard() {
            service->operation_finished();
        }
    } operation_guard { this };

    if (!websocket_ || closed_.load(std::memory_order_acquire)) {
        co_return;
    }

    running_.store(true, std::memory_order_release);
    if (accepted_socket) {
        websocket_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        websocket_->read_message_max(MAX_INBOUND_MESSAGE_BYTES);
        boost::system::error_code error;
        co_await websocket_->async_accept(asio::redirect_error(asio::use_awaitable, error));
        if (error) {
            report_error(Network::SocketServiceError::IncorrectHandshake, error.message());
            co_return;
        }
    }

    if (!co_await exchange_keys() || !co_await exchange_handshake()) {
        close_connection();
        co_return;
    }

    const auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    active_operations_.fetch_add(1, std::memory_order_acq_rel);
    asio::co_spawn(
        strand_,
        [self]() -> asio::awaitable<void> {
            struct OperationGuard final {
                WebSocketService* service;
                ~OperationGuard() {
                    service->operation_finished();
                }
            } operation_guard { self.get() };
            co_await self->write_loop();
        },
        [self](std::exception_ptr error) {
            if (error) {
                self->report_error(Network::SocketServiceError::Unknown, "write loop failed");
            }
        });
    co_await read_loop();
}

asio::awaitable<bool> WebSocketService::exchange_keys() {
    const auto encoded_key = Utils::to_base64(private_key_.public_key());
    if (!co_await write_text(encoded_key)) {
        report_error(Network::SocketServiceError::IncorrectPublicKey, "public key write failed");
        co_return false;
    }

    const auto received = co_await read_text();
    if (!received.has_value()) {
        report_error(Network::SocketServiceError::IncorrectPublicKey, "public key read failed");
        co_return false;
    }
    auto decoded = Utils::from_base64<std::vector<std::uint8_t>>(received.value());
    if (!decoded.has_value()) {
        report_error(Network::SocketServiceError::IncorrectPublicKey, "invalid public key encoding");
        co_return false;
    }

    public_key_          = KeyPublic(ByteArray(std::move(decoded.value())).toArray<crypto_sign_PUBLICKEYBYTES>());
    public_key_received_ = true;
    co_return true;
}

asio::awaitable<bool> WebSocketService::exchange_handshake() {
    Data encrypted;
    try {
        auto first_message = generate_first_message();
        encrypted          = co_await prepare_send_async(std::move(first_message));
    } catch (const std::exception& exception) {
        report_error(Network::SocketServiceError::IncorrectHandshake, exception.what());
        co_return false;
    }
    if (encrypted.empty() || !co_await write_text(Utils::to_base64(encrypted))) {
        report_error(Network::SocketServiceError::IncorrectHandshake, "handshake write failed");
        co_return false;
    }

    const auto received = co_await read_text();
    if (!received.has_value()) {
        report_error(Network::SocketServiceError::IncorrectFirstMessage, "handshake read failed");
        co_return false;
    }
    auto decoded = Utils::from_base64<std::vector<std::uint8_t>>(received.value());
    if (!decoded.has_value()) {
        report_error(Network::SocketServiceError::IncorrectFirstMessage, "invalid handshake encoding");
        co_return false;
    }
    Data decrypted;
    try {
        decrypted = co_await prepare_receive_async(std::move(decoded.value()));
    } catch (const std::exception& exception) {
        report_error(Network::SocketServiceError::IncorrectFirstMessage, exception.what());
        co_return false;
    }
    if (decrypted.empty()) {
        report_error(Network::SocketServiceError::IncorrectFirstMessage, "handshake decrypt failed");
        co_return false;
    }

    const std::string text(reinterpret_cast<const char*>(decrypted.data()), decrypted.size());
    if (text.starts_with("Error ")) {
        co_return false;
    }
    const auto handshake = Json::deserialize<HandshakeMessage>(text);
    if (!handshake.has_value()) {
        report_error(Network::SocketServiceError::IncorrectFirstMessage, handshake.error());
        co_return false;
    }
    co_return check_first_message(handshake.value());
}

asio::awaitable<bool> WebSocketService::write_text(std::string_view text) {
    if (!websocket_ || !websocket_->is_open()) {
        co_return false;
    }
    websocket_->text(true);
    boost::system::error_code error;
    socket_pending_bytes_.fetch_add(static_cast<std::int64_t>(text.size()), std::memory_order_relaxed);
    co_await websocket_->async_write(asio::buffer(text), asio::redirect_error(asio::use_awaitable, error));
    socket_pending_bytes_.fetch_sub(static_cast<std::int64_t>(text.size()), std::memory_order_relaxed);
    co_return !error;
}

asio::awaitable<std::optional<std::string>> WebSocketService::read_text() {
    if (!websocket_ || !websocket_->is_open()) {
        co_return std::nullopt;
    }
    beast::flat_buffer        buffer;
    boost::system::error_code error;
    co_await websocket_->async_read(buffer, asio::redirect_error(asio::use_awaitable, error));
    if (error || !websocket_->got_text()) {
        co_return std::nullopt;
    }
    co_return beast::buffers_to_string(buffer.data());
}

asio::awaitable<void> WebSocketService::read_loop() {
    beast::flat_buffer buffer;
    while (running_.load(std::memory_order_acquire) && websocket_ && websocket_->is_open()) {
        boost::system::error_code error;
        co_await websocket_->async_read(buffer, asio::redirect_error(asio::use_awaitable, error));
        if (error) {
            if (error != websocket::error::closed && error != asio::error::operation_aborted) {
                report_error(Network::SocketServiceError::Unknown, error.message());
            }
            break;
        }

        if (websocket_->got_text()) {
            eWarning("[WS] Unexpected text message after activation from {}", ip_);
        } else {
            std::vector<std::uint8_t> raw(buffer.size());
            asio::buffer_copy(asio::buffer(raw), buffer.data());
            try {
                co_await process_binary(std::move(raw));
            } catch (const std::exception& exception) {
                report_error(Network::SocketServiceError::Unknown, exception.what());
                break;
            }
        }
        buffer.consume(buffer.size());
    }
    close_connection();
}

asio::awaitable<void> WebSocketService::write_loop() {
    if (write_running_.exchange(true, std::memory_order_acq_rel)) {
        co_return;
    }

    while (running_.load(std::memory_order_acquire) && websocket_ && websocket_->is_open()) {
        Data data;
        {
            std::scoped_lock lock(queue_mutex_);
            if (!high_queue_.empty()) {
                data = std::move(high_queue_.front());
                high_queue_.pop();
            } else if (!normal_queue_.empty()) {
                data = std::move(normal_queue_.front());
                normal_queue_.pop();
            } else if (!low_queue_.empty()) {
                data = std::move(low_queue_.front());
                low_queue_.pop();
            }
            queued_bytes_.fetch_sub(static_cast<std::int64_t>(data.size()), std::memory_order_relaxed);
        }

        if (data.empty()) {
            queue_signal_.expires_at(std::chrono::steady_clock::time_point::max());
            boost::system::error_code ignored;
            co_await queue_signal_.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
            continue;
        }

        Data encrypted;
        try {
            encrypted = co_await prepare_send_async(std::move(data));
        } catch (const std::exception& exception) {
            report_error(Network::SocketServiceError::CantSend, exception.what());
            break;
        }
        if (encrypted.empty()) {
            report_error(Network::SocketServiceError::CantSend, "message encryption failed");
            break;
        }

        websocket_->binary(true);
        boost::system::error_code error;
        socket_pending_bytes_.fetch_add(static_cast<std::int64_t>(encrypted.size()), std::memory_order_relaxed);
        co_await websocket_->async_write(asio::buffer(encrypted),
                                         asio::redirect_error(asio::use_awaitable, error));
        socket_pending_bytes_.fetch_sub(static_cast<std::int64_t>(encrypted.size()), std::memory_order_relaxed);
        if (error) {
            report_error(Network::SocketServiceError::CantSend, error.message());
            break;
        }
    }

    write_running_.store(false, std::memory_order_release);
    close_connection();
}

asio::awaitable<WebSocketService::Data> WebSocketService::prepare_send_async(Data message) {
    if (message.size() < ASYNC_CRYPTO_THRESHOLD_BYTES) {
        co_return prepare_send_message(message);
    }
    const auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    co_return co_await runtime_.async_blocking([self, message = std::move(message)]() mutable {
        return self->prepare_send_message(message);
    });
}

asio::awaitable<WebSocketService::Data> WebSocketService::prepare_receive_async(Data message) {
    if (message.size() < ASYNC_CRYPTO_THRESHOLD_BYTES) {
        co_return prepare_receive_message(message);
    }
    const auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    co_return co_await runtime_.async_blocking([self, message = std::move(message)]() mutable {
        return self->prepare_receive_message(message);
    });
}

asio::awaitable<void> WebSocketService::process_binary(std::vector<std::uint8_t> message) {
    auto decrypted = co_await prepare_receive_async(std::move(message));
    if (decrypted.empty()) {
        report_error(Network::SocketServiceError::EmptyMessage, "message decrypt failed");
        co_return;
    }
    if (!context_.peer_processing_enabled()) {
        co_return;
    }
    if (on_message) {
        std::string text(reinterpret_cast<const char*>(decrypted.data()), decrypted.size());
        on_message(shared_from_this(), std::move(text), ip_, identifier_);
    }
}

bool WebSocketService::is_active() const {
    // The Beast stream is confined to strand_. Do not read its state from UI or
    // worker threads. The lifecycle atomics are updated on every open/close path.
    return activated_.load(std::memory_order_acquire) && running_.load(std::memory_order_acquire)
           && !closed_.load(std::memory_order_acquire);
}

std::string WebSocketService::protocol_string() const {
    return "WebSocket";
}

Network::Protocol WebSocketService::protocol() const {
    return Network::Protocol::WebSocket;
}

std::uint16_t WebSocketService::port() const {
    return port_;
}

std::uint16_t WebSocketService::server_port() const {
    return context_.local_server_port();
}

void WebSocketService::send_message(std::span<const std::uint8_t> data, Priority priority) {
    if (data.empty() || !is_active() || closed_.load(std::memory_order_acquire)) {
        return;
    }

    auto       payload = Data(data.begin(), data.end());
    const auto self    = std::static_pointer_cast<WebSocketService>(shared_from_this());
    asio::post(strand_, [self, payload = std::move(payload), priority]() mutable {
        if (!self->running_.load(std::memory_order_acquire)) {
            return;
        }
        const auto size = payload.size();
        {
            std::scoped_lock lock(self->queue_mutex_);
            switch (priority) {
            case Priority::High:
                self->high_queue_.push(std::move(payload));
                break;
            case Priority::Normal:
                self->normal_queue_.push(std::move(payload));
                break;
            case Priority::Low:
                self->low_queue_.push(std::move(payload));
                break;
            }
            self->queued_bytes_.fetch_add(static_cast<std::int64_t>(size), std::memory_order_relaxed);
        }
        self->queue_signal_.cancel();
    });
}

void WebSocketService::flush() {
    if (!is_active()) {
        return;
    }
    const auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    asio::post(strand_, [self] {
        self->queue_signal_.cancel();
    });
}

std::int64_t WebSocketService::pending_bytes() const noexcept {
    return queued_bytes_.load(std::memory_order_relaxed) + socket_pending_bytes_.load(std::memory_order_relaxed);
}

void WebSocketService::close_connection() {
    if (closed_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    running_.store(false, std::memory_order_release);
    activated_.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(queue_mutex_);
        std::queue<Data> empty;
        high_queue_.swap(empty);
        normal_queue_.swap(empty);
        low_queue_.swap(empty);
        queued_bytes_.store(0, std::memory_order_relaxed);
    }

    const auto self = std::static_pointer_cast<WebSocketService>(shared_from_this());
    asio::dispatch(strand_, [self] {
        self->finish_close();
    });
    SocketService::close_connection();
}

bool WebSocketService::wait_closed(std::chrono::milliseconds timeout) {
    std::unique_lock lock(close_mutex_);
    return close_condition_.wait_for(lock, timeout, [this] {
        return transport_closed_.load(std::memory_order_acquire);
    });
}

void WebSocketService::finish_close() {
    queue_signal_.cancel();
    if (websocket_) {
        boost::system::error_code ignored;
        beast::get_lowest_layer(*websocket_).cancel();
        beast::get_lowest_layer(*websocket_).socket().shutdown(Tcp::socket::shutdown_both, ignored);
        beast::get_lowest_layer(*websocket_).socket().close(ignored);
    }
    if (active_operations_.load(std::memory_order_acquire) == 0) {
        websocket_.reset();
        transport_closed_.store(true, std::memory_order_release);
        close_condition_.notify_all();
    }
}

void WebSocketService::operation_finished() {
    if (active_operations_.fetch_sub(1, std::memory_order_acq_rel) != 1) {
        return;
    }

    // All composed Beast operations have returned. The stream must be
    // destroyed while its io_context is still alive.
    websocket_.reset();
    transport_closed_.store(true, std::memory_order_release);
    close_condition_.notify_all();
}

void WebSocketService::report_error(Network::SocketServiceError code, std::string detail) {
    if (on_error) {
        on_error(shared_from_this(), code, detail, ip_, identifier_, direction_);
    }
    close_connection();
}
