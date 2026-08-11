/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "network/network_runtime.h"

#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/tcp_stream.hpp>

namespace ExtraChain::Core {

    NetworkRuntime::NetworkRuntime(RuntimeConfig config)
        : runtime_(config) {
        runtime_.start();
    }

    NetworkRuntime::~NetworkRuntime() {
        stop();
    }

    std::expected<std::uint16_t, std::string> NetworkRuntime::listen(std::uint16_t port, AcceptHandler handler) {
        if (stopping_.load(std::memory_order_acquire)) {
            return std::unexpected("network runtime is stopping");
        }
        if (listening()) {
            boost::system::error_code error;
            return acceptor_->local_endpoint(error).port();
        }

        {
            std::scoped_lock lock(accept_handler_mutex_);
            accept_handler_ = std::move(handler);
        }
        boost::system::error_code error;
        if (!open_acceptor(Tcp::v6(), port, error) && !open_acceptor(Tcp::v4(), port, error)) {
            std::scoped_lock lock(accept_handler_mutex_);
            accept_handler_ = {};
            return std::unexpected(error.message());
        }

        const auto local_port = acceptor_->local_endpoint(error).port();
        if (error) {
            stop_listening();
            return std::unexpected(error.message());
        }
        listening_.store(true, std::memory_order_release);
        boost::asio::co_spawn(runtime_.executor(), accept_loop(), boost::asio::detached);
        return local_port;
    }

    bool NetworkRuntime::open_acceptor(const Tcp& protocol, std::uint16_t port, boost::system::error_code& error) {
        error.clear();
        acceptor_ = std::make_unique<Tcp::acceptor>(runtime_.executor());
        acceptor_->open(protocol, error);
        if (!error && protocol == Tcp::v6()) {
            acceptor_->set_option(boost::asio::ip::v6_only(false), error);
        }
        if (!error) {
            acceptor_->set_option(boost::asio::socket_base::reuse_address(true), error);
        }
        if (!error) {
            acceptor_->bind(Tcp::endpoint(protocol, port), error);
        }
        if (!error) {
            acceptor_->listen(boost::asio::socket_base::max_listen_connections, error);
        }
        if (error) {
            boost::system::error_code ignored;
            acceptor_->close(ignored);
            acceptor_.reset();
            return false;
        }
        return true;
    }

    boost::asio::awaitable<void> NetworkRuntime::accept_loop() {
        auto* const acceptor = acceptor_.get();
        while (!stopping_.load(std::memory_order_acquire) && listening_.load(std::memory_order_acquire)) {
            boost::system::error_code error;
            auto                      socket =
                co_await acceptor->async_accept(boost::asio::redirect_error(boost::asio::use_awaitable, error));
            if (error) {
                co_return;
            }
            AcceptHandler handler;
            {
                std::scoped_lock lock(accept_handler_mutex_);
                handler = accept_handler_;
            }
            if (handler) {
                handler(std::move(socket));
            } else {
                socket.close(error);
            }
        }
    }

    void NetworkRuntime::stop_listening() {
        listening_.store(false, std::memory_order_release);
        {
            std::scoped_lock lock(accept_handler_mutex_);
            accept_handler_ = {};
        }
        if (!acceptor_) {
            return;
        }
        boost::system::error_code ignored;
        acceptor_->cancel(ignored);
        acceptor_->close(ignored);
    }

    void NetworkRuntime::stop() {
        if (stopping_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        stop_listening();
        runtime_.stop();
    }

    bool NetworkRuntime::listening() const noexcept {
        return listening_.load(std::memory_order_acquire);
    }

    Runtime::Executor NetworkRuntime::executor() {
        return runtime_.executor();
    }

    void NetworkRuntime::async_probe(std::string               host,
                                     std::uint16_t             port,
                                     std::chrono::milliseconds timeout,
                                     ProbeHandler              handler) {
        if (stopping_.load(std::memory_order_acquire)) {
            handler(false, "network runtime is stopping");
            return;
        }

        boost::asio::co_spawn(
            runtime_.executor(),
            [host = std::move(host), port, timeout, handler = std::move(handler)]() mutable
            -> boost::asio::awaitable<void> {
                const auto               executor = co_await boost::asio::this_coro::executor;
                Tcp::resolver            resolver(executor);
                boost::beast::tcp_stream stream(executor);
                stream.expires_after(timeout);
                boost::system::error_code error;
                const auto                endpoints =
                    co_await resolver.async_resolve(host,
                                                    std::to_string(port),
                                                    boost::asio::redirect_error(boost::asio::use_awaitable,
                                                                                error));
                if (!error) {
                    co_await stream.async_connect(endpoints,
                                                  boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }
                boost::system::error_code ignored;
                stream.socket().close(ignored);
                handler(!error, error.message());
            },
            boost::asio::detached);
    }

    std::expected<void, std::string> NetworkRuntime::probe(std::string_view          host,
                                                           std::uint16_t             port,
                                                           std::chrono::milliseconds timeout_value) {
        boost::asio::io_context   io_context;
        Tcp::resolver             resolver(io_context);
        boost::system::error_code resolve_error;
        const auto endpoints = resolver.resolve(std::string(host), std::to_string(port), resolve_error);
        if (resolve_error) {
            return std::unexpected(resolve_error.message());
        }
        Tcp::socket               socket(io_context);
        boost::asio::steady_timer timeout(io_context);
        boost::system::error_code connect_error = boost::asio::error::would_block;

        timeout.expires_after(timeout_value);
        timeout.async_wait([&socket](const boost::system::error_code& error) {
            if (!error) {
                boost::system::error_code ignored;
                socket.cancel(ignored);
            }
        });
        boost::asio::async_connect(socket,
                                   endpoints,
                                   [&connect_error, &timeout](const boost::system::error_code& error,
                                                              const Tcp::endpoint&) {
                                       connect_error = error;
                                       timeout.cancel();
                                   });
        io_context.run();

        boost::system::error_code ignored;
        socket.close(ignored);
        if (connect_error) {
            return std::unexpected(connect_error.message());
        }
        return {};
    }

} // namespace ExtraChain::Core
