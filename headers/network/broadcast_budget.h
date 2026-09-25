#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace Network {
    class BroadcastBudget {
    public:
        using Clock                                = std::chrono::steady_clock;
        static constexpr std::size_t PeerMessages  = 128;
        static constexpr std::size_t PeerBytes     = 4 * 1024 * 1024;
        static constexpr std::size_t TotalMessages = 1024;
        static constexpr std::size_t TotalBytes    = 32 * 1024 * 1024;
        static constexpr std::size_t MaxPeers      = 1024;

        bool accept(std::string_view peer, std::size_t bytes, Clock::time_point now = Clock::now()) {
            if (peer.empty() || peer.size() > 64 || bytes > PeerBytes)
                return false;
            std::lock_guard lock(mutex_);
            reset(total_, now);
            if (total_.messages >= TotalMessages || bytes > TotalBytes - total_.bytes)
                return false;
            auto found = peers_.find(peer);
            if (found == peers_.end()) {
                if (peers_.size() == MaxPeers) {
                    std::erase_if(peers_, [now](const auto& item) {
                        return now - item.second.start >= std::chrono::seconds(30);
                    });
                }
                if (peers_.size() == MaxPeers)
                    return false;
                found = peers_.emplace(std::string(peer), Window { .start = now }).first;
            }
            auto& window = found->second;
            reset(window, now);
            if (window.messages >= PeerMessages || bytes > PeerBytes - window.bytes)
                return false;
            ++window.messages;
            window.bytes += bytes;
            ++total_.messages;
            total_.bytes += bytes;
            return true;
        }

    private:
        struct Window {
            Clock::time_point start;
            std::size_t       messages = 0;
            std::size_t       bytes    = 0;
        };
        static void reset(Window& window, Clock::time_point now) {
            if (now - window.start >= std::chrono::seconds(1))
                window = { .start = now };
        }
        std::mutex                                 mutex_;
        Window                                     total_;
        std::map<std::string, Window, std::less<>> peers_;
    };
} // namespace Network
