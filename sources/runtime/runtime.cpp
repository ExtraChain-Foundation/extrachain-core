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
            , blocking_pool(runtime_config.blocking_threads) {
        }

        RuntimeConfig             config;
        boost::asio::io_context   io_context;
        std::optional<WorkGuard>  work_guard;
        boost::asio::thread_pool  blocking_pool;
        std::vector<std::jthread> io_threads;
        std::mutex                lifecycle_mutex;
        std::atomic_bool          running { false };
        bool                      stopped = false;
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
                                                         checked_thread_count(config.blocking_threads) })) {
    }

    Runtime::~Runtime() {
        stop();
    }

    void Runtime::start() {
        const auto state = state_;
        std::scoped_lock lock(state->lifecycle_mutex);
        if (state->stopped) {
            throw std::logic_error("ExtraChain Core runtime cannot restart after stop");
        }
        if (state->running.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        state->work_guard.emplace(boost::asio::make_work_guard(state->io_context));
        state->io_threads.reserve(state->config.io_threads);
        for (std::size_t index = 0; index < state->config.io_threads; ++index) {
            state->io_threads.emplace_back([state](std::stop_token) {
                state->io_context.run();
            });
        }
    }

    void Runtime::run() {
        const auto state = state_;
        {
            std::scoped_lock lock(state->lifecycle_mutex);
            if (state->stopped) {
                throw std::logic_error("ExtraChain Core runtime cannot restart after stop");
            }
            if (!state->running.exchange(true, std::memory_order_acq_rel)) {
                state->work_guard.emplace(boost::asio::make_work_guard(state->io_context));
            }
        }
        state->io_context.run();
    }

    void Runtime::stop() {
        const auto state = state_;
        std::vector<std::jthread> threads;
        {
            std::scoped_lock lock(state->lifecycle_mutex);
            if (state->stopped) {
                return;
            }
            state->stopped = true;
            state->running.store(false, std::memory_order_release);
            state->work_guard.reset();
            state->io_context.stop();
            threads.swap(state->io_threads);
        }

        const auto caller = std::this_thread::get_id();
        for (auto& thread : threads) {
            if (thread.get_id() == caller) {
                thread.detach();
            } else if (thread.joinable()) {
                thread.join();
            }
        }
        state->blocking_pool.stop();
        state->blocking_pool.join();
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

    boost::asio::thread_pool& Runtime::blocking_pool() noexcept {
        return state_->blocking_pool;
    }

} // namespace ExtraChain::Core
