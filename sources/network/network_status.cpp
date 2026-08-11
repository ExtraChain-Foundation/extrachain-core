/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "network/network_status.h"

#include <utility>

namespace ExtraChain::Core {

    NetworkStatus::Status NetworkStatus::status() const noexcept {
        return status_.load(std::memory_order_acquire);
    }

    bool NetworkStatus::update(Status status) {
        auto previous = status_.exchange(status, std::memory_order_acq_rel);
        if (previous == status) {
            return false;
        }
        changed_.publish(status);
        return true;
    }

    NetworkStatus::ChangedEvent::Connection NetworkStatus::subscribe(ChangedEvent::Slot slot) {
        return changed_.subscribe(std::move(slot));
    }

} // namespace ExtraChain::Core
