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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "runtime/runtime.h"

namespace ExtraChain::Core {

    struct NetworkConfig {
        std::string   bind_address;
        std::uint16_t port = 17593;
    };

    /**
     * Owns the Boost network execution context and the inbound TCP listener.
     *
     * This type has no Qt dependency. A Qt client can translate its callbacks
     * to signals without owning sockets or timers.
     */
    class NetworkRuntime final {
    public:
        using Tcp           = boost::asio::ip::tcp;
        using AcceptHandler = std::function<void(Tcp::socket)>;
        using ProbeHandler  = std::function<void(bool, std::string)>;
        using HttpResult    = std::expected<std::string, std::string>;
        using HttpHandler   = std::function<void(HttpResult)>;

        explicit NetworkRuntime(RuntimeConfig config = {});
        ~NetworkRuntime();

        NetworkRuntime(const NetworkRuntime&)            = delete;
        NetworkRuntime& operator=(const NetworkRuntime&) = delete;
        NetworkRuntime(NetworkRuntime&&)                 = delete;
        NetworkRuntime& operator=(NetworkRuntime&&)      = delete;

        [[nodiscard]] std::expected<std::uint16_t, std::string> listen(const NetworkConfig& config,
                                                                       AcceptHandler        handler);
        void                                                    stop_listening();
        void                                                    stop();

        [[nodiscard]] bool              listening() const noexcept;
        [[nodiscard]] Runtime::Executor executor();
        [[nodiscard]] Runtime::Executor storage_executor();
        [[nodiscard]] Runtime::Executor compute_executor();
        void                            spawn(boost::asio::awaitable<void> operation);

        template <typename Function>
        [[nodiscard]] auto async_blocking(Function function) {
            return runtime_.async_blocking(std::move(function));
        }

        void                                                         async_probe(std::string               host,
                                                                                 std::uint16_t             port,
                                                                                 std::chrono::milliseconds timeout,
                                                                                 ProbeHandler              handler);
        void                                                         async_http_get(std::string               host,
                                                                                    std::uint16_t             port,
                                                                                    std::string               target,
                                                                                    std::chrono::milliseconds timeout,
                                                                                    HttpHandler               handler);
        void                                                         async_http_post(std::string               host,
                                                                                     std::uint16_t             port,
                                                                                     std::string               target,
                                                                                     std::string               content_type,
                                                                                     std::string               body,
                                                                                     std::chrono::milliseconds timeout,
                                                                                     HttpHandler               handler);
        [[nodiscard]] static std::expected<void, std::string>        probe(std::string_view          host,
                                                                           std::uint16_t             port,
                                                                           std::chrono::milliseconds timeout);
        [[nodiscard]] static std::expected<std::string, std::string> local_address();

    private:
        struct AsyncOperation;

        [[nodiscard]] std::expected<std::uint16_t, std::string> listen_on_executor(NetworkConfig config,
                                                                                   AcceptHandler handler);
        void                                                    stop_listening_on_executor();
        void                         register_operation(const std::shared_ptr<AsyncOperation>& operation);
        void                         cancel_operations();
        [[nodiscard]] bool open_acceptor(const Tcp::endpoint& endpoint, boost::system::error_code& error);
        boost::asio::awaitable<void> accept_loop(std::shared_ptr<Tcp::acceptor> acceptor, AcceptHandler handler);

        Runtime                                    runtime_;
        std::shared_ptr<Tcp::acceptor>             acceptor_;
        std::mutex                                 operations_mutex_;
        std::vector<std::weak_ptr<AsyncOperation>> operations_;
        std::atomic_bool                           listening_ { false };
        std::atomic_bool                           stopping_ { false };
    };

} // namespace ExtraChain::Core
