/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "runtime/periodic_task.h"

#include <cstdint>
#include <stdexcept>
#include <utility>

#include <boost/asio/dispatch.hpp>

namespace ExtraChain::Core {

    struct PeriodicTask::State final : std::enable_shared_from_this<PeriodicTask::State> {
        State(boost::asio::any_io_executor executor, Duration interval_value, Handler handler_value)
            : strand(boost::asio::make_strand(std::move(executor)))
            , timer(strand)
            , interval(interval_value)
            , handler(std::move(handler_value)) {
        }

        void schedule(std::uint64_t expected_generation) {
            timer.expires_after(interval);
            std::weak_ptr<State> weak = shared_from_this();
            timer.async_wait([weak, expected_generation](const boost::system::error_code& error) {
                const auto self = weak.lock();
                if (!self || !self->active.load(std::memory_order_acquire)
                    || self->generation.load(std::memory_order_acquire) != expected_generation) {
                    return;
                }
                if (error == boost::asio::error::operation_aborted) {
                    return;
                }
                if (error) {
                    self->active.store(false, std::memory_order_release);
                    return;
                }

                self->handler();
                if (self->active.load(std::memory_order_acquire)
                    && self->generation.load(std::memory_order_acquire) == expected_generation) {
                    self->schedule(expected_generation);
                }
            });
        }

        boost::asio::strand<boost::asio::any_io_executor> strand;
        boost::asio::steady_timer                         timer;
        Duration                                          interval;
        Handler                                           handler;
        std::atomic_bool                                  active { false };
        std::atomic_uint64_t                              generation { 0 };
    };

    std::shared_ptr<PeriodicTask> PeriodicTask::create(boost::asio::any_io_executor executor,
                                                       Duration                     interval,
                                                       Handler                      handler) {
        if (interval <= Duration::zero()) {
            throw std::invalid_argument("PeriodicTask interval must be positive");
        }
        if (!handler) {
            throw std::invalid_argument("PeriodicTask handler is required");
        }
        return std::shared_ptr<PeriodicTask>(new PeriodicTask(std::move(executor), interval, std::move(handler)));
    }

    PeriodicTask::PeriodicTask(boost::asio::any_io_executor executor, Duration interval, Handler handler)
        : state_(std::make_shared<State>(std::move(executor), interval, std::move(handler))) {
    }

    PeriodicTask::~PeriodicTask() {
        stop();
    }

    void PeriodicTask::start() {
        if (state_->active.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        const auto generation = state_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        boost::asio::dispatch(state_->strand, [state = state_, generation] {
            if (state->active.load(std::memory_order_acquire)
                && state->generation.load(std::memory_order_acquire) == generation) {
                state->timer.cancel();
                state->schedule(generation);
            }
        });
    }

    void PeriodicTask::stop() {
        if (!state_ || !state_->active.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        const auto generation = state_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        boost::asio::dispatch(state_->strand, [state = state_, generation] {
            if (state->generation.load(std::memory_order_acquire) == generation) {
                state->timer.cancel();
            }
        });
    }

    void PeriodicTask::set_interval(Duration interval) {
        if (interval <= Duration::zero()) {
            throw std::invalid_argument("PeriodicTask interval must be positive");
        }
        const auto generation = state_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        boost::asio::dispatch(state_->strand, [state = state_, interval, generation] {
            if (state->generation.load(std::memory_order_acquire) != generation) {
                return;
            }
            state->interval = interval;
            if (state->active.load(std::memory_order_acquire)) {
                state->timer.cancel();
                state->schedule(generation);
            }
        });
    }

    bool PeriodicTask::active() const noexcept {
        return state_->active.load(std::memory_order_acquire);
    }

} // namespace ExtraChain::Core
