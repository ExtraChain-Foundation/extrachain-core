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

#include "runtime/event.h"

namespace ExtraChain::Core {

    class NetworkStatus final {
    public:
        enum class Status {
            Unknown,
            Online,
            Offline,
            Local
        };

        using ChangedEvent = Event<Status>;

        [[nodiscard]] Status status() const noexcept;
        bool                 update(Status status);

        [[nodiscard]] ChangedEvent::Connection subscribe(ChangedEvent::Slot slot);

    private:
        std::atomic<Status> status_ { Status::Offline };
        ChangedEvent        changed_;
    };

} // namespace ExtraChain::Core
