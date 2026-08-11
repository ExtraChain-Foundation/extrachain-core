/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "network/traffic_meter.h"

#include <mutex>

namespace ExtraChain::Core {

    TrafficMeter* TrafficMeter::get_instance() noexcept {
        static TrafficMeter meter;
        return &meter;
    }

    void TrafficMeter::add_bytes_sent(const std::string& peer, std::uint64_t bytes) {
        {
            std::unique_lock lock(mutex_);
            peers_[peer].bytes_sent += bytes;
        }
        total_sent_.fetch_add(bytes, std::memory_order_relaxed);
    }

    void TrafficMeter::add_bytes_received(const std::string& peer, std::uint64_t bytes) {
        {
            std::unique_lock lock(mutex_);
            peers_[peer].bytes_received += bytes;
        }
        total_received_.fetch_add(bytes, std::memory_order_relaxed);
    }

    std::uint64_t TrafficMeter::total_bytes_sent_from_connection(const std::string& peer) const {
        std::shared_lock lock(mutex_);
        const auto       entry = peers_.find(peer);
        return entry == peers_.end() ? 0 : entry->second.bytes_sent;
    }

    std::uint64_t TrafficMeter::total_bytes_received_from_connection(const std::string& peer) const {
        std::shared_lock lock(mutex_);
        const auto       entry = peers_.find(peer);
        return entry == peers_.end() ? 0 : entry->second.bytes_received;
    }

    std::pair<std::uint64_t, std::uint64_t> TrafficMeter::total_bytes() const noexcept {
        return { total_sent_.load(std::memory_order_relaxed), total_received_.load(std::memory_order_relaxed) };
    }

} // namespace ExtraChain::Core
