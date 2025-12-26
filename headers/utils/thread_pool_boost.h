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

#ifdef _WIN32
    #include <winsock2.h>
#endif

#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/dispatch.hpp>

class ThreadPoolBoost {
public:
    ThreadPoolBoost() = delete;

    static std::shared_ptr<ThreadPoolBoost> instance_dfs(size_t threads_count = 1);
    static std::shared_ptr<ThreadPoolBoost> instance_dag(size_t threads_count = 1);
    static std::shared_ptr<ThreadPoolBoost> instance_dag_sync(size_t threads_count = 8);
    static std::shared_ptr<ThreadPoolBoost> instance(size_t threads_count = 1);

    static void terminate();

    template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) NullaryToken>
    auto post(NullaryToken&& nullary_token) {
        return boost::asio::post(*m_thread_pool, nullary_token);
        // auto safe_wrapper = [token = std::forward<NullaryToken>(nullary_token)]() mutable {
        //     try {
        //         token();
        //     } catch (const std::exception& e) {
        //         std::cerr << "Exception in posted task: " << e.what() << std::endl;
        //     } catch (...) {
        //         std::cerr << "Unknown exception in posted task." << std::endl;
        //     }
        // };

        // return boost::asio::post(*m_thread_pool, std::move(safe_wrapper));
    }

    template <BOOST_ASIO_COMPLETION_TOKEN_FOR(void()) NullaryToken>
    auto dispatch(NullaryToken&& nullary_token) {
        return boost::asio::dispatch(*m_thread_pool, nullary_token);
    }

    void join();

private:
    ThreadPoolBoost(size_t threads_count);

    static void initialize(boost::asio::thread_pool& thread_pool, const size_t threads_count);

    std::unique_ptr<boost::asio::thread_pool> m_thread_pool;
};
