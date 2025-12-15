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

#include "network/http_client.h"
#include "utils/exc_logs.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

HttpClient::HttpClient(asio::io_context& ioc)
    : ioc_(ioc) {
}

void HttpClient::post_async(
    const std::string& host,
    uint16_t port,
    const std::string& target,
    const std::string& body,
    const std::string& content_type,
    std::function<void(bool success, std::string response)> callback,
    bool use_ssl
) {
#ifndef EXTRACHAIN_SSL_ENABLED
    if (use_ssl) {
        callback(false, "SSL not enabled (compile with EXTRACHAIN_SSL=ON)");
        return;
    }
#endif

    asio::co_spawn(ioc_, [=, this]() -> asio::awaitable<void> {
        try {
            tcp::resolver resolver(ioc_);
            auto [ec_r, results] = co_await resolver.async_resolve(
                host, std::to_string(port), asio::as_tuple(asio::use_awaitable));

            if (ec_r) {
                callback(false, ec_r.message());
                co_return;
            }

#ifdef EXTRACHAIN_SSL_ENABLED
            if (use_ssl) {
                asio::ssl::context ctx(asio::ssl::context::tlsv12_client);
                ctx.set_default_verify_paths();
                ctx.set_verify_mode(asio::ssl::verify_peer);

                beast::ssl_stream<beast::tcp_stream> stream(ioc_, ctx);

                if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                    callback(false, "Failed to set SNI hostname");
                    co_return;
                }

                beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));

                auto [ec_c, ep] = co_await beast::get_lowest_layer(stream).async_connect(
                    results, asio::as_tuple(asio::use_awaitable));

                if (ec_c) {
                    callback(false, ec_c.message());
                    co_return;
                }

                auto [ec_hs] = co_await stream.async_handshake(
                    asio::ssl::stream_base::client, asio::as_tuple(asio::use_awaitable));

                if (ec_hs) {
                    callback(false, "SSL handshake failed: " + ec_hs.message());
                    co_return;
                }

                http::request<http::string_body> req{http::verb::post, target, 11};
                req.set(http::field::host, host);
                req.set(http::field::user_agent, "ExtraChain/1.0");
                req.set(http::field::content_type, content_type);
                req.body() = body;
                req.prepare_payload();

                co_await http::async_write(stream, req, asio::use_awaitable);

                beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, asio::use_awaitable);

                stream.async_shutdown([](beast::error_code) {});

                callback(true, res.body());
                co_return;
            }
#endif

            // Plain HTTP
            beast::tcp_stream stream(ioc_);
            stream.expires_after(std::chrono::seconds(30));

            auto [ec_c, ep] = co_await stream.async_connect(
                results, asio::as_tuple(asio::use_awaitable));

            if (ec_c) {
                callback(false, ec_c.message());
                co_return;
            }

            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "ExtraChain/1.0");
            req.set(http::field::content_type, content_type);
            req.body() = body;
            req.prepare_payload();

            auto [ec_w, bytes_w] = co_await http::async_write(
                stream, req, asio::as_tuple(asio::use_awaitable));

            if (ec_w) {
                callback(false, ec_w.message());
                co_return;
            }

            beast::flat_buffer buffer;
            http::response<http::string_body> res;

            auto [ec_read, bytes_r] = co_await http::async_read(
                stream, buffer, res, asio::as_tuple(asio::use_awaitable));

            if (ec_read) {
                callback(false, ec_read.message());
                co_return;
            }

            beast::error_code ec_shutdown;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec_shutdown);

            callback(true, res.body());
        } catch (const std::exception& e) {
            callback(false, e.what());
        }
        co_return;
    }, asio::detached);
}

void HttpClient::get_async(
    const std::string& host,
    uint16_t port,
    const std::string& target,
    std::function<void(bool success, std::string response)> callback,
    bool use_ssl
) {
#ifndef EXTRACHAIN_SSL_ENABLED
    if (use_ssl) {
        callback(false, "SSL not enabled (compile with EXTRACHAIN_SSL=ON)");
        return;
    }
#endif

    asio::co_spawn(ioc_, [=, this]() -> asio::awaitable<void> {
        try {
            tcp::resolver resolver(ioc_);
            auto [ec_r, results] = co_await resolver.async_resolve(
                host, std::to_string(port), asio::as_tuple(asio::use_awaitable));

            if (ec_r) {
                callback(false, ec_r.message());
                co_return;
            }

#ifdef EXTRACHAIN_SSL_ENABLED
            if (use_ssl) {
                asio::ssl::context ctx(asio::ssl::context::tlsv12_client);
                ctx.set_default_verify_paths();
                ctx.set_verify_mode(asio::ssl::verify_peer);

                beast::ssl_stream<beast::tcp_stream> stream(ioc_, ctx);

                if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                    callback(false, "Failed to set SNI hostname");
                    co_return;
                }

                beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));

                auto [ec_c, ep] = co_await beast::get_lowest_layer(stream).async_connect(
                    results, asio::as_tuple(asio::use_awaitable));

                if (ec_c) {
                    callback(false, ec_c.message());
                    co_return;
                }

                auto [ec_hs] = co_await stream.async_handshake(
                    asio::ssl::stream_base::client, asio::as_tuple(asio::use_awaitable));

                if (ec_hs) {
                    callback(false, "SSL handshake failed: " + ec_hs.message());
                    co_return;
                }

                http::request<http::empty_body> req{http::verb::get, target, 11};
                req.set(http::field::host, host);
                req.set(http::field::user_agent, "ExtraChain/1.0");

                co_await http::async_write(stream, req, asio::use_awaitable);

                beast::flat_buffer buffer;
                http::response<http::string_body> res;
                co_await http::async_read(stream, buffer, res, asio::use_awaitable);

                stream.async_shutdown([](beast::error_code) {});

                callback(true, res.body());
                co_return;
            }
#endif

            // Plain HTTP
            beast::tcp_stream stream(ioc_);
            stream.expires_after(std::chrono::seconds(30));

            auto [ec_c, ep] = co_await stream.async_connect(
                results, asio::as_tuple(asio::use_awaitable));

            if (ec_c) {
                callback(false, ec_c.message());
                co_return;
            }

            http::request<http::empty_body> req{http::verb::get, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "ExtraChain/1.0");

            auto [ec_w, bytes_w] = co_await http::async_write(
                stream, req, asio::as_tuple(asio::use_awaitable));

            if (ec_w) {
                callback(false, ec_w.message());
                co_return;
            }

            beast::flat_buffer buffer;
            http::response<http::string_body> res;

            auto [ec_read, bytes_r] = co_await http::async_read(
                stream, buffer, res, asio::as_tuple(asio::use_awaitable));

            if (ec_read) {
                callback(false, ec_read.message());
                co_return;
            }

            beast::error_code ec_shutdown;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec_shutdown);

            callback(true, res.body());
        } catch (const std::exception& e) {
            callback(false, e.what());
        }
        co_return;
    }, asio::detached);
}

std::expected<std::string, std::string> HttpClient::post_sync(
    const std::string& host,
    uint16_t port,
    const std::string& target,
    const std::string& body,
    const std::string& content_type,
    bool use_ssl
) {
#ifndef EXTRACHAIN_SSL_ENABLED
    if (use_ssl) {
        return std::unexpected("SSL not enabled (compile with EXTRACHAIN_SSL=ON)");
    }
#endif

    try {
        asio::io_context ioc;

        tcp::resolver resolver(ioc);
        auto results = resolver.resolve(host, std::to_string(port));

#ifdef EXTRACHAIN_SSL_ENABLED
        if (use_ssl) {
            asio::ssl::context ctx(asio::ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(asio::ssl::verify_peer);

            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                return std::unexpected("Failed to set SNI hostname");
            }

            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
            beast::get_lowest_layer(stream).connect(results);

            stream.handshake(asio::ssl::stream_base::client);

            http::request<http::string_body> req{http::verb::post, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "ExtraChain/1.0");
            req.set(http::field::content_type, content_type);
            req.body() = body;
            req.prepare_payload();

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            return res.body();
        }
#endif

        // Plain HTTP
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::seconds(30));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "ExtraChain/1.0");
        req.set(http::field::content_type, content_type);
        req.body() = body;
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return res.body();
    } catch (const std::exception& e) {
        return std::unexpected(e.what());
    }
}

std::expected<std::string, std::string> HttpClient::get_sync(
    const std::string& host,
    uint16_t port,
    const std::string& target,
    bool use_ssl
) {
#ifndef EXTRACHAIN_SSL_ENABLED
    if (use_ssl) {
        return std::unexpected("SSL not enabled (compile with EXTRACHAIN_SSL=ON)");
    }
#endif

    try {
        asio::io_context ioc;

        tcp::resolver resolver(ioc);
        auto results = resolver.resolve(host, std::to_string(port));

#ifdef EXTRACHAIN_SSL_ENABLED
        if (use_ssl) {
            asio::ssl::context ctx(asio::ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(asio::ssl::verify_peer);

            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

            if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                return std::unexpected("Failed to set SNI hostname");
            }

            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
            beast::get_lowest_layer(stream).connect(results);

            stream.handshake(asio::ssl::stream_base::client);

            http::request<http::empty_body> req{http::verb::get, target, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, "ExtraChain/1.0");

            http::write(stream, req);

            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);

            return res.body();
        }
#endif

        // Plain HTTP
        beast::tcp_stream stream(ioc);
        stream.expires_after(std::chrono::seconds(30));
        stream.connect(results);

        http::request<http::empty_body> req{http::verb::get, target, 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "ExtraChain/1.0");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return res.body();
    } catch (const std::exception& e) {
        return std::unexpected(e.what());
    }
}
