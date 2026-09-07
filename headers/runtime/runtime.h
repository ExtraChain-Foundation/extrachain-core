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
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "extrachain_global.h"

namespace ExtraChain::Core {

    struct RuntimeConfig {
        std::size_t io_threads      = 1;
        std::size_t storage_threads = 1;
        std::size_t compute_threads = 1;
    };

    /**
     * Owns all asynchronous Core execution resources.
     *
     * Network and timer handlers use the io_context. Database and filesystem
     * work uses the storage pool. CPU-bound and WebAssembly work uses the
     * compute pool.
     */
    class Runtime final {
    public:
        using Executor = boost::asio::any_io_executor;

        explicit Runtime(RuntimeConfig config = {});
        ~Runtime();

        Runtime(const Runtime&)            = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime(Runtime&&)                 = delete;
        Runtime& operator=(Runtime&&)      = delete;

        void start();
        void run();
        void request_stop();
        void join();
        void stop();

        [[nodiscard]] bool                      running() const noexcept;
        [[nodiscard]] Executor                  executor();
        [[nodiscard]] boost::asio::io_context&  io_context() noexcept;
        [[nodiscard]] Executor                  storage_executor() noexcept;
        [[nodiscard]] Executor                  compute_executor() noexcept;
        [[nodiscard]] boost::asio::thread_pool& storage_pool() noexcept;
        [[nodiscard]] boost::asio::thread_pool& compute_pool() noexcept;

        template <typename Function>
        [[nodiscard]] static auto guard_handler(const char* boundary, Function&& function) {
            return [boundary, function = std::forward<Function>(function)]() mutable noexcept {
                try {
                    std::invoke(function);
                } catch (const std::exception& error) {
                    report_handler_exception(boundary, error.what());
                } catch (...) {
                    report_handler_exception(boundary, "non-standard exception");
                }
            };
        }

        /**
         * Run CPU-bound work on the bounded compute pool.
         *
         * The coroutine resumes on its original executor. This keeps CPU-bound
         * and WebAssembly work away from network and timer threads.
         */
        template <typename Function>
        [[nodiscard]] boost::asio::awaitable<std::invoke_result_t<Function&>> async_blocking(Function function) {
            using Result = std::invoke_result_t<Function&>;
            static_assert(!std::is_reference_v<Result>, "async_blocking does not return references");

            if constexpr (std::is_void_v<Result>) {
                co_await boost::asio::co_spawn(
                    compute_pool(),
                    [function = std::move(function)]() mutable -> boost::asio::awaitable<void> {
                        std::invoke(function);
                        co_return;
                    },
                    boost::asio::use_awaitable);
                co_return;
            } else {
                co_return co_await boost::asio::co_spawn(
                    compute_pool(),
                    [function = std::move(function)]() mutable -> boost::asio::awaitable<Result> {
                        co_return std::invoke(function);
                    },
                    boost::asio::use_awaitable);
            }
        }

    private:
        EXTRACHAIN_EXPORT static void report_handler_exception(const char* boundary, const char* detail) noexcept;
        struct State;
        std::shared_ptr<State> state_;
    };

} // namespace ExtraChain::Core
