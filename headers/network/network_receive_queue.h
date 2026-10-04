#pragma once

#include "extrachain_global.h"

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct NetworkReceivedMessage {
    std::string message;
    std::string ip;
    std::string identifier;
    bool maintenance = false;

    std::size_t bytes() const { return message.size() + ip.size() + identifier.size(); }
};

// One owner-thread dispatch at a time, including all of its deferred SQL stages
class EXTRACHAIN_EXPORT NetworkReceiveQueue final : public QObject {
public:
    struct Limits {
        std::size_t messages;
        std::size_t bytes;
    };
    using Handler = std::function<void(const NetworkReceivedMessage &)>;
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    NetworkReceiveQueue(QObject *parent, Handler handler, ErrorHandler error = {},
                        Limits limits = { 1024, 64 * 1024 * 1024 });
    ~NetworkReceiveQueue() override;

    bool enqueue(const std::string &message, const std::string &ip, const std::string &identifier);
    bool enqueue_maintenance();
    void defer(std::function<void()> work, std::function<void()> continuation);
    void stop();
    std::size_t pending_count() const;
    std::size_t pending_bytes() const;

private:
    struct Work {
        std::function<void()> operation;
        std::function<void()> continuation;
    };
    struct Completion {
        std::function<void()> continuation;
        std::exception_ptr error;
    };

    void schedule();
    void drain();
    void dispatch();
    void complete(std::function<void()> continuation, std::exception_ptr error);
    void finish_turn();
    void finish_active();
    void report(std::exception_ptr error);
    void worker_loop();

    const Limits limits_;
    Handler handler_;
    ErrorHandler error_handler_;
    mutable std::mutex queue_mutex_;
    std::deque<NetworkReceivedMessage> queue_;
    std::optional<NetworkReceivedMessage> active_;
    std::size_t message_count_ = 0;
    std::size_t bytes_ = 0;
    bool maintenance_pending_ = false;
    bool wake_pending_ = false;
    bool draining_ = false;
    bool invoking_ = false;
    bool deferred_ = false;
    bool active_failed_ = false;
    std::optional<Completion> completion_;
    std::atomic<bool> stopped_ = false;
    std::mutex work_mutex_;
    std::condition_variable work_ready_;
    std::optional<Work> work_;
    std::thread worker_;
};
