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

#include <algorithm>
#include <utility>

#include <boost/asio/connect.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

namespace ExtraChain::Core {

    struct NetworkRuntime::AsyncOperation final {
        explicit AsyncOperation(std::function<void()> cancel_handler)
            : cancel_handler(std::move(cancel_handler)) {
        }

        void cancel() {
            if (!finished.exchange(true, std::memory_order_acq_rel)) {
                cancel_handler();
            }
        }

        void complete() {
            finished.store(true, std::memory_order_release);
        }

        std::function<void()> cancel_handler;
        std::atomic_bool      finished { false };
    };

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
        cancel_operations();
        runtime_.stop();
    }

    bool NetworkRuntime::listening() const noexcept {
        return listening_.load(std::memory_order_acquire);
    }

    Runtime::Executor NetworkRuntime::executor() {
        return runtime_.executor();
    }

    void NetworkRuntime::register_operation(const std::shared_ptr<AsyncOperation>& operation) {
        std::scoped_lock lock(operations_mutex_);
        std::erase_if(operations_, [](const auto& current) {
            return current.expired();
        });
        operations_.push_back(operation);
    }

    void NetworkRuntime::cancel_operations() {
        std::vector<std::shared_ptr<AsyncOperation>> operations;
        {
            std::scoped_lock lock(operations_mutex_);
            operations.reserve(operations_.size());
            for (const auto& weak : operations_) {
                if (auto operation = weak.lock()) {
                    operations.push_back(std::move(operation));
                }
            }
            operations_.clear();
        }
        for (const auto& operation : operations) {
            operation->cancel();
        }
    }

    void NetworkRuntime::async_probe(std::string               host,
                                     std::uint16_t             port,
                                     std::chrono::milliseconds timeout,
                                     ProbeHandler              handler) {
        if (stopping_.load(std::memory_order_acquire)) {
            handler(false, "network runtime is stopping");
            return;
        }

        const auto resolver = std::make_shared<Tcp::resolver>(runtime_.executor());
        const auto stream   = std::make_shared<boost::beast::tcp_stream>(runtime_.executor());
        const auto operation =
            std::make_shared<AsyncOperation>([executor = runtime_.executor(), resolver, stream] {
                boost::asio::dispatch(executor, [resolver, stream] {
                    resolver->cancel();
                    boost::system::error_code ignored;
                    stream->cancel();
                    stream->socket().close(ignored);
                });
            });
        register_operation(operation);
        boost::asio::co_spawn(
            runtime_.executor(),
            [host = std::move(host),
             port,
             timeout,
             handler = std::move(handler),
             resolver,
             stream,
             operation]() mutable -> boost::asio::awaitable<void> {
                const auto                executor = co_await boost::asio::this_coro::executor;
                boost::asio::steady_timer resolve_timeout(executor);
                resolve_timeout.expires_after(timeout);
                resolve_timeout.async_wait([resolver](const boost::system::error_code& timer_error) {
                    if (!timer_error) {
                        resolver->cancel();
                    }
                });
                stream->expires_after(timeout);
                boost::system::error_code error;
                const auto                endpoints =
                    co_await resolver->async_resolve(host,
                                                     std::to_string(port),
                                                     boost::asio::redirect_error(boost::asio::use_awaitable,
                                                                                 error));
                resolve_timeout.cancel();
                if (!error) {
                    co_await stream->async_connect(endpoints,
                                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }
                boost::system::error_code ignored;
                stream->socket().close(ignored);
                operation->complete();
                handler(!error, error.message());
            },
            boost::asio::detached);
    }

    void NetworkRuntime::async_http_get(std::string               host,
                                        std::uint16_t             port,
                                        std::string               target,
                                        std::chrono::milliseconds timeout,
                                        HttpHandler               handler) {
        if (stopping_.load(std::memory_order_acquire)) {
            handler(std::unexpected("network runtime is stopping"));
            return;
        }

        const auto resolver = std::make_shared<Tcp::resolver>(runtime_.executor());
        const auto stream   = std::make_shared<boost::beast::tcp_stream>(runtime_.executor());
        const auto operation =
            std::make_shared<AsyncOperation>([executor = runtime_.executor(), resolver, stream] {
                boost::asio::dispatch(executor, [resolver, stream] {
                    resolver->cancel();
                    boost::system::error_code ignored;
                    stream->cancel();
                    stream->socket().close(ignored);
                });
            });
        register_operation(operation);
        boost::asio::co_spawn(
            runtime_.executor(),
            [host = std::move(host),
             port,
             target = std::move(target),
             timeout,
             handler = std::move(handler),
             resolver,
             stream,
             operation]() mutable -> boost::asio::awaitable<void> {
                namespace http = boost::beast::http;

                const auto                executor = co_await boost::asio::this_coro::executor;
                boost::asio::steady_timer resolve_timeout(executor);
                resolve_timeout.expires_after(timeout);
                resolve_timeout.async_wait([resolver](const boost::system::error_code& timer_error) {
                    if (!timer_error) {
                        resolver->cancel();
                    }
                });
                boost::system::error_code error;
                const auto                endpoints =
                    co_await resolver->async_resolve(host,
                                                     std::to_string(port),
                                                     boost::asio::redirect_error(boost::asio::use_awaitable,
                                                                                 error));
                resolve_timeout.cancel();
                if (!error) {
                    stream->expires_after(timeout);
                    co_await stream->async_connect(endpoints,
                                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }

                http::request<http::empty_body> request(http::verb::get, target, 11);
                request.set(http::field::host, host);
                request.set(http::field::user_agent, "ExtraChain-Core");
                if (!error) {
                    co_await http::async_write(*stream,
                                               request,
                                               boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }

                boost::beast::flat_buffer                buffer;
                http::response_parser<http::string_body> parser;
                parser.body_limit(1024 * 1024);
                if (!error) {
                    co_await http::async_read(*stream,
                                              buffer,
                                              parser,
                                              boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }

                boost::system::error_code ignored;
                stream->socket().shutdown(Tcp::socket::shutdown_both, ignored);
                stream->socket().close(ignored);
                operation->complete();
                if (error) {
                    handler(std::unexpected(error.message()));
                    co_return;
                }

                auto response = parser.release();
                if (response.result() != http::status::ok) {
                    handler(std::unexpected("HTTP status " + std::to_string(response.result_int())));
                    co_return;
                }
                handler(std::move(response.body()));
            },
            boost::asio::detached);
    }

    void NetworkRuntime::async_http_post(std::string               host,
                                         std::uint16_t             port,
                                         std::string               target,
                                         std::string               content_type,
                                         std::string               body,
                                         std::chrono::milliseconds timeout,
                                         HttpHandler               handler) {
        if (stopping_.load(std::memory_order_acquire)) {
            handler(std::unexpected("network runtime is stopping"));
            return;
        }

        const auto resolver = std::make_shared<Tcp::resolver>(runtime_.executor());
        const auto stream   = std::make_shared<boost::beast::tcp_stream>(runtime_.executor());
        const auto operation =
            std::make_shared<AsyncOperation>([executor = runtime_.executor(), resolver, stream] {
                boost::asio::dispatch(executor, [resolver, stream] {
                    resolver->cancel();
                    boost::system::error_code ignored;
                    stream->cancel();
                    stream->socket().close(ignored);
                });
            });
        register_operation(operation);
        boost::asio::co_spawn(
            runtime_.executor(),
            [host = std::move(host),
             port,
             target       = std::move(target),
             content_type = std::move(content_type),
             body         = std::move(body),
             timeout,
             handler = std::move(handler),
             resolver,
             stream,
             operation]() mutable -> boost::asio::awaitable<void> {
                namespace http = boost::beast::http;

                const auto                executor = co_await boost::asio::this_coro::executor;
                boost::asio::steady_timer resolve_timeout(executor);
                resolve_timeout.expires_after(timeout);
                resolve_timeout.async_wait([resolver](const boost::system::error_code& timer_error) {
                    if (!timer_error) {
                        resolver->cancel();
                    }
                });
                boost::system::error_code error;
                const auto                endpoints =
                    co_await resolver->async_resolve(host,
                                                     std::to_string(port),
                                                     boost::asio::redirect_error(boost::asio::use_awaitable,
                                                                                 error));
                resolve_timeout.cancel();
                if (!error) {
                    stream->expires_after(timeout);
                    co_await stream->async_connect(endpoints,
                                                   boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }

                http::request<http::string_body> request(http::verb::post, target, 11);
                request.set(http::field::host, host);
                request.set(http::field::user_agent, "ExtraChain-Core");
                request.set(http::field::content_type, content_type);
                request.body() = std::move(body);
                request.prepare_payload();
                if (!error) {
                    co_await http::async_write(*stream,
                                               request,
                                               boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }

                boost::beast::flat_buffer                buffer;
                http::response_parser<http::string_body> parser;
                parser.body_limit(1024 * 1024);
                if (!error) {
                    co_await http::async_read(*stream,
                                              buffer,
                                              parser,
                                              boost::asio::redirect_error(boost::asio::use_awaitable, error));
                }

                boost::system::error_code ignored;
                stream->socket().shutdown(Tcp::socket::shutdown_both, ignored);
                stream->socket().close(ignored);
                operation->complete();
                if (error) {
                    handler(std::unexpected(error.message()));
                    co_return;
                }

                auto response = parser.release();
                if (response.result() != http::status::ok) {
                    handler(std::unexpected("HTTP status " + std::to_string(response.result_int())));
                    co_return;
                }
                handler(std::move(response.body()));
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

    std::expected<std::string, std::string> NetworkRuntime::local_address() {
        boost::asio::io_context      io_context;
        boost::asio::ip::udp::socket socket(io_context);
        boost::system::error_code    error;
        socket.open(boost::asio::ip::udp::v4(), error);
        if (error) {
            return std::unexpected(error.message());
        }

        socket.connect({ boost::asio::ip::make_address_v4("8.8.8.8"), 53 }, error);
        if (error) {
            return std::unexpected(error.message());
        }

        const auto endpoint = socket.local_endpoint(error);
        if (error) {
            return std::unexpected(error.message());
        }
        return endpoint.address().to_string();
    }

} // namespace ExtraChain::Core
