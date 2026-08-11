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
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace ExtraChain::Core {

    class TrafficMeter final {
    public:
        static TrafficMeter* get_instance() noexcept;

        TrafficMeter(const TrafficMeter&)            = delete;
        TrafficMeter& operator=(const TrafficMeter&) = delete;

        void add_bytes_sent(const std::string& peer, std::uint64_t bytes);
        void add_bytes_received(const std::string& peer, std::uint64_t bytes);

        [[nodiscard]] std::uint64_t total_bytes_sent_from_connection(const std::string& peer) const;
        [[nodiscard]] std::uint64_t total_bytes_received_from_connection(const std::string& peer) const;
        [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> total_bytes() const noexcept;

    private:
        struct PeerTraffic {
            std::uint64_t bytes_sent     = 0;
            std::uint64_t bytes_received = 0;
        };

        TrafficMeter() = default;

        mutable std::shared_mutex                    mutex_;
        std::unordered_map<std::string, PeerTraffic> peers_;
        std::atomic_uint64_t                         total_sent_ { 0 };
        std::atomic_uint64_t                         total_received_ { 0 };
    };

} // namespace ExtraChain::Core
