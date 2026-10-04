#include "network/network_receive_queue.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <stdexcept>
#include <utility>

NetworkReceiveQueue::NetworkReceiveQueue(QObject *parent, Handler handler, ErrorHandler error, Limits limits)
    : QObject(parent), limits_(limits), handler_(std::move(handler)), error_handler_(std::move(error)) {
    if (!handler_ || limits_.messages == 0 || limits_.bytes == 0) {
        throw std::invalid_argument("Invalid network receive queue configuration");
    }
    worker_ = std::thread([this] { worker_loop(); });
}

NetworkReceiveQueue::~NetworkReceiveQueue() {
    stop();
}

bool NetworkReceiveQueue::enqueue(const std::string &message, const std::string &ip,
                                  const std::string &identifier) {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopped_ || message_count_ >= limits_.messages || message.size() > limits_.bytes - bytes_) {
            return false;
        }
        auto remaining = limits_.bytes - bytes_ - message.size();
        if (ip.size() > remaining || identifier.size() > remaining - ip.size()) {
            return false;
        }
        queue_.push_back({ message, ip, identifier, false });
        bytes_ += queue_.back().bytes();
        ++message_count_;
    }
    schedule();
    return true;
}

bool NetworkReceiveQueue::enqueue_maintenance() {
    {
        std::lock_guard lock(queue_mutex_);
        if (stopped_) {
            return false;
        }
        if (maintenance_pending_) {
            return true;
        }
        // Reserve one coalesced maintenance item even when packet capacity is full
        queue_.push_back({ {}, {}, {}, true });
        maintenance_pending_ = true;
    }
    schedule();
    return true;
}

void NetworkReceiveQueue::schedule() {
    std::lock_guard lock(queue_mutex_);
    if (stopped_ || wake_pending_ || active_ || queue_.empty()) {
        return;
    }
    wake_pending_ = true;
    QMetaObject::invokeMethod(this, [this] { dispatch(); }, Qt::QueuedConnection);
}

void NetworkReceiveQueue::dispatch() {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        std::lock_guard lock(queue_mutex_);
        wake_pending_ = false;
        if (stopped_ || active_ || queue_.empty()) {
            return;
        }
        active_ = std::move(queue_.front());
        queue_.pop_front();
    }
    QPointer<NetworkReceiveQueue> guard(this);
    invoking_ = true;
    active_failed_ = false;
    try {
        auto handler = handler_;
        handler(*active_);
    } catch (...) {
        if (!guard) {
            return;
        }
        active_failed_ = true;
        report(std::current_exception());
    }
    if (!guard) {
        return;
    }
    finish_turn();
}

void NetworkReceiveQueue::defer(std::function<void()> work, std::function<void()> continuation) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (stopped_ || !invoking_ || deferred_ || !work || !continuation) {
        throw std::logic_error("Invalid deferred receive stage");
    }
    {
        std::lock_guard lock(work_mutex_);
        if (work_) {
            throw std::logic_error("Receive worker already has a pending stage");
        }
        work_.emplace(Work { std::move(work), std::move(continuation) });
        deferred_ = true;
    }
    work_ready_.notify_one();
}

void NetworkReceiveQueue::worker_loop() {
    for (;;) {
        Work next;
        {
            std::unique_lock lock(work_mutex_);
            work_ready_.wait(lock, [this] { return stopped_ || work_.has_value(); });
            if (!work_) {
                return;
            }
            next = std::move(*work_);
            work_.reset();
        }
        std::exception_ptr error;
        try {
            next.operation();
        } catch (...) {
            error = std::current_exception();
        }
        if (!stopped_) {
            QMetaObject::invokeMethod(this,
                [this, continuation = std::move(next.continuation), error]() mutable {
                    complete(std::move(continuation), error);
                }, Qt::QueuedConnection);
        }
    }
}

void NetworkReceiveQueue::complete(std::function<void()> continuation, std::exception_ptr error) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (stopped_) {
        return;
    }
    if (invoking_) {
        // A nested Qt event loop must not finish the packet still borrowed by its caller
        completion_.emplace(Completion { std::move(continuation), error });
        return;
    }
    QPointer<NetworkReceiveQueue> guard(this);
    deferred_ = false;
    invoking_ = true;
    try {
        if (error) {
            active_failed_ = true;
            report(error);
        } else if (!active_failed_) {
            continuation();
        }
    } catch (...) {
        if (!guard) {
            return;
        }
        active_failed_ = true;
        report(std::current_exception());
    }
    if (!guard) {
        return;
    }
    finish_turn();
}

void NetworkReceiveQueue::finish_turn() {
    invoking_ = false;
    if (completion_) {
        auto completion = std::move(*completion_);
        completion_.reset();
        QMetaObject::invokeMethod(this, [this, completion = std::move(completion)]() mutable {
            complete(std::move(completion.continuation), completion.error);
        }, Qt::QueuedConnection);
    } else if (!deferred_) {
        finish_active();
    }
}

void NetworkReceiveQueue::report(std::exception_ptr error) {
    if (!stopped_ && error_handler_) {
        auto handler = error_handler_;
        try {
            handler(error);
        } catch (...) {
            // Error reporting cannot strand the receive queue
        }
    }
}

void NetworkReceiveQueue::finish_active() {
    {
        std::lock_guard lock(queue_mutex_);
        if (!active_) {
            return;
        }
        if (!stopped_) {
            if (active_->maintenance) {
                maintenance_pending_ = false;
            } else {
                --message_count_;
                bytes_ -= active_->bytes();
            }
        }
        active_.reset();
    }
    schedule();
}

void NetworkReceiveQueue::stop() {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        std::lock_guard lock(work_mutex_);
        stopped_ = true;
    }
    work_ready_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::lock_guard lock(queue_mutex_);
    queue_.clear();
    if (!invoking_) {
        active_.reset();
    }
    completion_.reset();
    message_count_ = 0;
    bytes_ = 0;
    maintenance_pending_ = false;
    wake_pending_ = false;
    deferred_ = false;
}

std::size_t NetworkReceiveQueue::pending_count() const {
    std::lock_guard lock(queue_mutex_);
    return message_count_ + std::size_t(maintenance_pending_);
}

std::size_t NetworkReceiveQueue::pending_bytes() const {
    std::lock_guard lock(queue_mutex_);
    return bytes_;
}
