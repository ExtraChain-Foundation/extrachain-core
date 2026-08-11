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
#include <functional>
#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/thread_pool.hpp>

namespace ExtraChain::Core {

    struct RuntimeConfig {
        std::size_t io_threads       = 1;
        std::size_t blocking_threads = 1;
    };

    /**
     * Owns all asynchronous Core execution resources.
     *
     * Network and timer handlers use the io_context. Blocking database,
     * filesystem, and WebAssembly work uses the bounded blocking pool.
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
        void stop();

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] Executor executor();
        [[nodiscard]] boost::asio::io_context& io_context() noexcept;
        [[nodiscard]] boost::asio::thread_pool& blocking_pool() noexcept;

    private:
        struct State;
        std::shared_ptr<State> state_;
    };

} // namespace ExtraChain::Core
