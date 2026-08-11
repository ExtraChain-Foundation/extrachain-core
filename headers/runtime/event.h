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

#include <utility>

#include <boost/signals2/connection.hpp>
#include <boost/signals2/signal.hpp>

namespace ExtraChain::Core {

    template <typename... Args>
    class Event final {
    public:
        using Slot       = typename boost::signals2::signal<void(Args...)>::slot_type;
        using Connection = boost::signals2::scoped_connection;

        Event() = default;

        Event(const Event&)            = delete;
        Event& operator=(const Event&) = delete;
        Event(Event&&)                 = delete;
        Event& operator=(Event&&)      = delete;

        [[nodiscard]] Connection subscribe(Slot slot) {
            return Connection(signal_.connect(std::move(slot)));
        }

        void publish(Args... args) {
            signal_(std::forward<Args>(args)...);
        }

        void disconnect_all() {
            signal_.disconnect_all_slots();
        }

    private:
        boost::signals2::signal<void(Args...)> signal_;
    };

} // namespace ExtraChain::Core
