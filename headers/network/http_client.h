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

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#ifdef EXTRACHAIN_SSL_ENABLED
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#endif

#include <functional>
#include <string>
#include <expected>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

class HttpClient {
public:
    explicit HttpClient(asio::io_context& ioc);

    // Async POST with callback
    void post_async(
        const std::string& host,
        uint16_t port,
        const std::string& target,
        const std::string& body,
        const std::string& content_type,
        std::function<void(bool success, std::string response)> callback,
        bool use_ssl = false
    );

    // Async GET with callback
    void get_async(
        const std::string& host,
        uint16_t port,
        const std::string& target,
        std::function<void(bool success, std::string response)> callback,
        bool use_ssl = false
    );

    // Sync POST (blocking)
    std::expected<std::string, std::string> post_sync(
        const std::string& host,
        uint16_t port,
        const std::string& target,
        const std::string& body,
        const std::string& content_type,
        bool use_ssl = false
    );

    // Sync GET (blocking)
    std::expected<std::string, std::string> get_sync(
        const std::string& host,
        uint16_t port,
        const std::string& target,
        bool use_ssl = false
    );

    // Check if SSL is available
    static constexpr bool ssl_available() {
#ifdef EXTRACHAIN_SSL_ENABLED
        return true;
#else
        return false;
#endif
    }

private:
    asio::io_context& ioc_;
};
