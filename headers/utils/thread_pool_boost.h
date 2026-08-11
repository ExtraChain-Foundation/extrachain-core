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

#include <cstddef>
#include <memory>
#include <utility>

#ifdef _WIN32
    #include <winsock2.h>
#endif

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/dispatch.hpp>

class ThreadPoolBoost {
public:
    ThreadPoolBoost() = delete;

    // 4, not 1: eleven different DFS jobs share this pool — dirs sync, file fragments,
    // vector content, vector row writes. Since sqlite writes now wait for a contended
    // lock instead of failing (sqlite3_busy_timeout), a single worker can stall the
    // whole queue for seconds. Measured on a six-node stand with one thread: vector
    // replication dropped to ~1 file per 45s, a hundredfold slowdown, while the same
    // work took two minutes before. Per-vector ordering is unaffected — concurrent
    // writers to one file are serialised by sqlite itself.
    static std::shared_ptr<ThreadPoolBoost> instance_dfs(std::size_t threads_count = 4);
    static std::shared_ptr<ThreadPoolBoost> instance_dag(std::size_t threads_count = 1);
    static std::shared_ptr<ThreadPoolBoost> instance_dag_sync(std::size_t threads_count = 8);
    static std::shared_ptr<ThreadPoolBoost> instance(std::size_t threads_count = 1);

    static void terminate();

    template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) NullaryToken>
    auto post(NullaryToken&& nullary_token) {
        return boost::asio::post(*m_thread_pool, std::forward<NullaryToken>(nullary_token));
    }

    template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) NullaryToken>
    auto dispatch(NullaryToken&& nullary_token) {
        return boost::asio::dispatch(*m_thread_pool, std::forward<NullaryToken>(nullary_token));
    }

    void join();

private:
    ThreadPoolBoost(std::size_t threads_count);

    std::unique_ptr<boost::asio::thread_pool> m_thread_pool;
};
