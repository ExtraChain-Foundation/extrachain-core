/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "runtime/runtime.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include <boost/asio/executor_work_guard.hpp>

namespace ExtraChain::Core {

    struct Runtime::State final {
        using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

        explicit State(RuntimeConfig runtime_config)
            : config(runtime_config)
            , storage_pool(runtime_config.storage_threads)
            , compute_pool(runtime_config.compute_threads) {
        }

        RuntimeConfig            config;
        boost::asio::io_context  io_context;
        std::optional<WorkGuard> work_guard;
        boost::asio::thread_pool storage_pool;
        boost::asio::thread_pool compute_pool;
        std::vector<std::thread> io_threads;
        std::mutex               lifecycle_mutex;
        std::atomic_bool         running { false };
        bool                     stop_requested = false;
        bool                     joined         = false;
    };

    namespace {
        std::size_t checked_thread_count(std::size_t value) {
            if (value == 0) {
                throw std::invalid_argument("ExtraChain Core runtime requires at least one thread");
            }
            return value;
        }
    } // namespace

    Runtime::Runtime(RuntimeConfig config)
        : state_(std::make_shared<State>(RuntimeConfig { checked_thread_count(config.io_threads),
                                                         checked_thread_count(config.storage_threads),
                                                         checked_thread_count(config.compute_threads) })) {
    }

    Runtime::~Runtime() {
        stop();
    }

    void Runtime::start() {
        const auto       state = state_;
        std::scoped_lock lock(state->lifecycle_mutex);
        if (state->stop_requested) {
            throw std::logic_error("ExtraChain Core runtime cannot restart after stop");
        }
        if (state->running.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        state->work_guard.emplace(boost::asio::make_work_guard(state->io_context));
        state->io_threads.reserve(state->config.io_threads);
        for (std::size_t index = 0; index < state->config.io_threads; ++index) {
            state->io_threads.emplace_back([state]() {
                state->io_context.run();
            });
        }
    }

    void Runtime::run() {
        const auto state = state_;
        {
            std::scoped_lock lock(state->lifecycle_mutex);
            if (state->stop_requested) {
                throw std::logic_error("ExtraChain Core runtime cannot restart after stop");
            }
            if (!state->running.exchange(true, std::memory_order_acq_rel)) {
                state->work_guard.emplace(boost::asio::make_work_guard(state->io_context));
            }
        }
        state->io_context.run();
    }

    void Runtime::request_stop() {
        const auto       state = state_;
        std::scoped_lock lock(state->lifecycle_mutex);
        if (state->stop_requested) {
            return;
        }
        state->stop_requested = true;
        state->running.store(false, std::memory_order_release);
    }

    void Runtime::join() {
        const auto               state = state_;
        std::vector<std::thread> threads;
        {
            std::scoped_lock lock(state->lifecycle_mutex);
            if (state->joined) {
                return;
            }
            const auto caller = std::this_thread::get_id();
            if (std::ranges::any_of(state->io_threads, [caller](const auto& thread) {
                    return thread.get_id() == caller;
                })) {
                throw std::logic_error("ExtraChain Core runtime must be joined outside its I/O workers");
            }
            state->joined = true;
            threads.swap(state->io_threads);
        }

        state->storage_pool.join();
        state->compute_pool.join();
        {
            std::scoped_lock lock(state->lifecycle_mutex);
            state->work_guard.reset();
        }
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void Runtime::stop() {
        request_stop();
        join();
    }

    bool Runtime::running() const noexcept {
        return state_->running.load(std::memory_order_acquire);
    }

    Runtime::Executor Runtime::executor() {
        return state_->io_context.get_executor();
    }

    boost::asio::io_context& Runtime::io_context() noexcept {
        return state_->io_context;
    }

    Runtime::Executor Runtime::storage_executor() noexcept {
        return state_->storage_pool.get_executor();
    }

    Runtime::Executor Runtime::compute_executor() noexcept {
        return state_->compute_pool.get_executor();
    }

    boost::asio::thread_pool& Runtime::storage_pool() noexcept {
        return state_->storage_pool;
    }

    boost::asio::thread_pool& Runtime::compute_pool() noexcept {
        return state_->compute_pool;
    }

} // namespace ExtraChain::Core
