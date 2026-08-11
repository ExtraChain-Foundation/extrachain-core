/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "runtime/deadline_task.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

namespace ExtraChain::Core {

    struct DeadlineTask::State final : std::enable_shared_from_this<DeadlineTask::State> {
        State(boost::asio::any_io_executor executor, Handler handler_value)
            : strand(boost::asio::make_strand(std::move(executor)))
            , timer(strand)
            , handler(std::move(handler_value)) {
        }

        void arm(Duration delay) {
            active.store(true, std::memory_order_release);
            ++generation;
            const auto expected_generation = generation;
            timer.expires_after(delay);
            std::weak_ptr<State> weak = shared_from_this();
            timer.async_wait([weak, expected_generation](const boost::system::error_code& error) {
                const auto self = weak.lock();
                if (!self || error == boost::asio::error::operation_aborted
                    || expected_generation != self->generation) {
                    return;
                }
                self->active.store(false, std::memory_order_release);
                if (!error) {
                    self->handler();
                }
            });
        }

        boost::asio::strand<boost::asio::any_io_executor> strand;
        boost::asio::steady_timer                         timer;
        Handler                                           handler;
        std::atomic_bool                                  active { false };
        std::uint64_t                                     generation = 0;
    };

    std::shared_ptr<DeadlineTask> DeadlineTask::create(boost::asio::any_io_executor executor, Handler handler) {
        if (!handler) {
            throw std::invalid_argument("DeadlineTask handler is required");
        }
        return std::shared_ptr<DeadlineTask>(new DeadlineTask(std::move(executor), std::move(handler)));
    }

    DeadlineTask::DeadlineTask(boost::asio::any_io_executor executor, Handler handler)
        : state_(std::make_shared<State>(std::move(executor), std::move(handler))) {
    }

    DeadlineTask::~DeadlineTask() {
        cancel();
    }

    void DeadlineTask::schedule_after(Duration delay) {
        if (delay < Duration::zero()) {
            throw std::invalid_argument("DeadlineTask delay cannot be negative");
        }
        boost::asio::dispatch(state_->strand, [state = state_, delay] {
            state->timer.cancel();
            state->arm(delay);
        });
    }

    void DeadlineTask::schedule_earlier(Duration delay) {
        if (delay < Duration::zero()) {
            throw std::invalid_argument("DeadlineTask delay cannot be negative");
        }
        boost::asio::dispatch(state_->strand, [state = state_, delay] {
            const auto requested = std::chrono::steady_clock::now() + delay;
            if (state->active.load(std::memory_order_acquire) && state->timer.expiry() <= requested) {
                return;
            }
            state->timer.cancel();
            state->arm(delay);
        });
    }

    void DeadlineTask::cancel() {
        if (!state_) {
            return;
        }
        state_->active.store(false, std::memory_order_release);
        boost::asio::dispatch(state_->strand, [state = state_] {
            ++state->generation;
            state->timer.cancel();
        });
    }

    bool DeadlineTask::active() const noexcept {
        return state_->active.load(std::memory_order_acquire);
    }

} // namespace ExtraChain::Core
