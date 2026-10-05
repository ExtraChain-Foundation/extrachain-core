#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>

class NetworkReceiveQueue;

// Shared independently of QObject lifetime; snapshots never touch packet data
class NetworkReceiveMetrics {
public:
    using Clock = std::chrono::steady_clock;
    enum class Stage { Pending, WorkerWait, WorkerRun, Delivery, Owner, Active, Count };

    QJsonObject snapshot() const {
        State state;
        {
            std::lock_guard lock(mutex_);
            state = state_;
        }
        QJsonArray timings;
        constexpr std::array names { "pending", "worker-wait", "worker-run", "delivery", "owner", "active" };
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto &timing = state.timings[i];
            timings.append(QJsonObject { { "stage", names[i] }, { "count", double(timing.count) },
                { "totalUs", double(timing.total_us) }, { "maxUs", double(timing.max_us) } });
        }
        return { { "version", 1 }, { "stopped", state.stopped },
            { "uptimeMs", double(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - created_).count()) },
            { "accepted", double(state.accepted) }, { "rejected", double(state.rejected) },
            { "started", double(state.started) }, { "completed", double(state.completed) },
            { "pending", double(state.pending) }, { "pendingBytes", double(state.bytes) },
            { "peakPending", double(state.peak_pending) }, { "peakBytes", double(state.peak_bytes) },
            { "timings", timings } };
    }

private:
    friend class NetworkReceiveQueue;
    struct Timing {
        std::uint64_t count = 0;
        std::uint64_t total_us = 0;
        std::uint64_t max_us = 0;
    };
    struct State {
        std::uint64_t accepted = 0, rejected = 0, started = 0, completed = 0;
        std::size_t pending = 0, bytes = 0, peak_pending = 0, peak_bytes = 0;
        bool stopped = false;
        std::array<Timing, static_cast<std::size_t>(Stage::Count)> timings {};
    };

    void timing(Stage stage, Clock::time_point started, Clock::time_point ended = Clock::now()) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(ended - started).count();
        std::lock_guard lock(mutex_);
        auto &value = state_.timings[static_cast<std::size_t>(stage)];
        ++value.count;
        const auto duration = static_cast<std::uint64_t>(std::max<std::int64_t>(0, elapsed));
        value.total_us += duration;
        value.max_us = std::max(value.max_us, duration);
    }

    void pending(std::size_t count, std::size_t bytes) {
        std::lock_guard lock(mutex_);
        state_.pending = count;
        state_.bytes = bytes;
        state_.peak_pending = std::max(state_.peak_pending, count);
        state_.peak_bytes = std::max(state_.peak_bytes, bytes);
    }

    void increment(std::uint64_t State::*field) {
        std::lock_guard lock(mutex_);
        ++(state_.*field);
    }

    void stop() {
        std::lock_guard lock(mutex_);
        state_.stopped = true;
        state_.pending = 0;
        state_.bytes = 0;
    }

    const Clock::time_point created_ = Clock::now();
    mutable std::mutex mutex_;
    State state_;
};
