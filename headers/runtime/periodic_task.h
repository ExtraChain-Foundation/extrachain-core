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
#include <chrono>
#include <functional>
#include <memory>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

namespace ExtraChain::Core {

    class PeriodicTask final {
    public:
        using Duration = std::chrono::steady_clock::duration;
        using Handler  = std::function<void()>;

        static std::shared_ptr<PeriodicTask> create(boost::asio::any_io_executor executor,
                                                     Duration                     interval,
                                                     Handler                      handler);

        ~PeriodicTask();

        PeriodicTask(const PeriodicTask&)            = delete;
        PeriodicTask& operator=(const PeriodicTask&) = delete;

        void start();
        void stop();
        void set_interval(Duration interval);

        [[nodiscard]] bool active() const noexcept;

    private:
        struct State;

        PeriodicTask(boost::asio::any_io_executor executor, Duration interval, Handler handler);

        std::shared_ptr<State> state_;
    };

} // namespace ExtraChain::Core
