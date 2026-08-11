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

namespace ExtraChain::Core {

    class DeadlineTask final {
    public:
        using Duration = std::chrono::steady_clock::duration;
        using Handler  = std::function<void()>;

        static std::shared_ptr<DeadlineTask> create(boost::asio::any_io_executor executor, Handler handler);

        ~DeadlineTask();

        DeadlineTask(const DeadlineTask&)            = delete;
        DeadlineTask& operator=(const DeadlineTask&) = delete;

        void schedule_after(Duration delay);
        void schedule_earlier(Duration delay);
        void cancel();

        [[nodiscard]] bool active() const noexcept;

    private:
        struct State;

        DeadlineTask(boost::asio::any_io_executor executor, Handler handler);

        std::shared_ptr<State> state_;
    };

} // namespace ExtraChain::Core
