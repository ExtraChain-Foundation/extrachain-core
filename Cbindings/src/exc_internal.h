/*
 * ExtraChain Core — C FFI Internal Helpers
 * NOT part of the public API. Do not include from consumer code.
 */

#pragma once

#include "../extrachain_c.h"

#include <any>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/post.hpp>
#include <boost/signals2/connection.hpp>

#include "core/extrachain_node.h"

namespace exc_ffi {

    /* ── String helpers ──────────────────────────────────────────────── */

    inline char* exc_strdup(const std::string& s) {
        char* p = static_cast<char*>(std::malloc(s.size() + 1));
        if (p) {
            std::memcpy(p, s.c_str(), s.size() + 1);
        }
        return p;
    }

    inline char* exc_strdup(const char* s) {
        if (!s)
            return nullptr;
        size_t len = std::strlen(s);
        char*  p   = static_cast<char*>(std::malloc(len + 1));
        if (p) {
            std::memcpy(p, s, len + 1);
        }
        return p;
    }

    /* ── Handle table ────────────────────────────────────────────────── */

    class HandleTable {
    public:
        static HandleTable& instance() {
            static HandleTable ht;
            return ht;
        }

        template <typename T>
        ExcHandle store(T&& value) {
            std::lock_guard lock(mutex_);
            const ExcHandle handle = next_++;
            entries_.emplace(handle,
                             Entry { std::any(std::forward<T>(value)), std::type_index(typeid(std::decay_t<T>)) });
            return handle;
        }

        template <typename T, typename Function>
        bool with(ExcHandle h, Function&& function) {
            std::lock_guard lock(mutex_);
            auto            it = entries_.find(h);
            if (it == entries_.end() || it->second.type != std::type_index(typeid(T))) {
                return false;
            }
            std::invoke(std::forward<Function>(function), *std::any_cast<T>(&it->second.value));
            return true;
        }

        void release(ExcHandle h) {
            std::lock_guard lock(mutex_);
            entries_.erase(h);
        }

    private:
        HandleTable()
            : next_(1) {
        }

        struct Entry {
            std::any        value;
            std::type_index type { typeid(void) };
        };

        std::mutex                           mutex_;
        ExcHandle                            next_;
        std::unordered_map<ExcHandle, Entry> entries_;
    };

    /* ── Global state ────────────────────────────────────────────────── */

    struct GlobalState {
        std::mutex                                        mutex;
        std::condition_variable                           stopped;
        std::condition_variable                           calls_idle;
        std::unique_ptr<ExtraChain::Core::ExtraChainNode> node;
        std::vector<boost::signals2::scoped_connection>   event_connections;
        std::atomic_bool                                  initialized { false };
        bool                                              main_thread_mode     = false;
        bool                                              initializing         = false;
        bool                                              shutdown_requested   = false;
        bool                                              shutdown_in_progress = false;
        std::size_t                                       active_calls         = 0;
        std::jthread                                      shutdown_worker;

        static GlobalState& instance() {
            static GlobalState gs;
            return gs;
        }
    };

    inline ExtraChain::Core::ExtraChainNode* begin_call() {
        auto&           gs = GlobalState::instance();
        std::lock_guard lock(gs.mutex);
        if (!gs.initialized || !gs.node) {
            return nullptr;
        }
        ++gs.active_calls;
        return gs.node.get();
    }

    inline void end_call() {
        auto& gs = GlobalState::instance();
        {
            std::lock_guard lock(gs.mutex);
            --gs.active_calls;
        }
        gs.calls_idle.notify_all();
    }

    class ActiveCallGuard {
    public:
        ActiveCallGuard() = default;
        ~ActiveCallGuard() {
            end_call();
        }

        ActiveCallGuard(const ActiveCallGuard&)            = delete;
        ActiveCallGuard& operator=(const ActiveCallGuard&) = delete;
    };

    inline thread_local std::size_t event_dispatch_depth = 0;

    class EventDispatchGuard {
    public:
        EventDispatchGuard() {
            auto&           gs = GlobalState::instance();
            std::lock_guard lock(gs.mutex);
            ++active_dispatches_;
            ++event_dispatch_depth;
        }

        ~EventDispatchGuard() {
            auto& gs = GlobalState::instance();
            {
                std::lock_guard lock(gs.mutex);
                --active_dispatches_;
                --event_dispatch_depth;
            }
            idle_.notify_all();
        }

        EventDispatchGuard(const EventDispatchGuard&)            = delete;
        EventDispatchGuard& operator=(const EventDispatchGuard&) = delete;

        static void wait_until_idle() {
            auto&            gs = GlobalState::instance();
            std::unique_lock lock(gs.mutex);
            idle_.wait(lock, [] {
                return active_dispatches_ == 0;
            });
        }

    private:
        inline static std::size_t             active_dispatches_ = 0;
        inline static std::condition_variable idle_;
    };

    /* ── Dispatch helpers ────────────────────────────────────────────── */

    /*
     * Run a function on the Core dispatch strand synchronously.
     * If already on the strand, run it directly.
     * Returns false if dispatch failed.
     */
    inline bool dispatch_sync(std::function<void()> fn) {
        auto* node = begin_call();
        if (!node)
            return false;
        ActiveCallGuard call_guard;

        if (node->on_serial_executor()) {
            try {
                fn();
                return true;
            } catch (...) {
                return false;
            }
        }

        auto completion = std::make_shared<std::promise<void>>();
        auto future     = completion->get_future();
        try {
            boost::asio::post(node->serial_executor(), [fn = std::move(fn), completion]() mutable {
                try {
                    fn();
                    completion->set_value();
                } catch (...) {
                    completion->set_exception(std::current_exception());
                }
            });
        } catch (...) {
            return false;
        }
        try {
            future.get();
            return true;
        } catch (...) {
            return false;
        }
    }

    /*
     * Run a function on the Core dispatch strand asynchronously.
     * Returns false if dispatch failed.
     */
    inline bool dispatch_async(std::function<void()> fn) {
        auto* node = begin_call();
        if (!node)
            return false;
        try {
            boost::asio::post(node->serial_executor(), [fn = std::move(fn)]() mutable {
                ActiveCallGuard call_guard;
                try {
                    fn();
                } catch (...) {
                }
            });
            return true;
        } catch (...) {
            end_call();
            return false;
        }
    }

    /* ── Callback registry ──────────────────────────────────────────── */

    template <typename CallbackType>
    class CallbackSlot {
    public:
        void set(CallbackType callback, ExcUserData user_data) {
            std::lock_guard lock(mutex_);
            callback_  = callback;
            user_data_ = user_data;
        }

        template <typename... Args>
        bool invoke(Args&&... args) const {
            CallbackType callback;
            ExcUserData  user_data;
            {
                std::lock_guard lock(mutex_);
                callback  = callback_;
                user_data = user_data_;
            }
            if (callback) {
                callback(std::forward<Args>(args)..., user_data);
                return true;
            }
            return false;
        }

    private:
        mutable std::mutex mutex_;
        CallbackType       callback_  = nullptr;
        ExcUserData        user_data_ = nullptr;
    };

    struct CallbackRegistry {
        CallbackSlot<ExcNodeReadyCallback> node_ready;

        CallbackSlot<ExcDagSyncStartCallback>    dag_sync_start;
        CallbackSlot<ExcDagSyncProgressCallback> dag_sync_progress;
        CallbackSlot<ExcDagSyncFinishCallback>   dag_sync_finish;
        CallbackSlot<ExcDagStatusCallback>       dag_status;

        CallbackSlot<ExcMiningStatusCallback> mining_status;

        CallbackSlot<ExcDagTxSendedCallback>      dag_tx_sended;
        CallbackSlot<ExcDagTxApprovedCallback>    dag_tx_approved;
        CallbackSlot<ExcDagTxNotApprovedCallback> dag_tx_not_approved;
        CallbackSlot<ExcSelfTxCallback>           self_tx;

        CallbackSlot<ExcChatMessageCallback> chat_message;
        CallbackSlot<ExcChatsLoadedCallback> chats_loaded;
        CallbackSlot<ExcChatAddedCallback>   chat_added;

        CallbackSlot<ExcDfsStoredCallback>           dfs_stored;
        CallbackSlot<ExcDfsDownloadedCallback>       dfs_downloaded;
        CallbackSlot<ExcDfsDownloadProgressCallback> dfs_download_progress;

        CallbackSlot<ExcConnectionStatusCallback> connection_status;
        CallbackSlot<ExcConnectionCountCallback>  connection_count;

        CallbackSlot<ExcActorRenamedCallback> actor_renamed;

        static CallbackRegistry& instance() {
            static CallbackRegistry cr;
            return cr;
        }
    };

    /* ── Convenience macros ──────────────────────────────────────────── */

#define EXC_CHECK_INIT()                                                                                          \
    do {                                                                                                          \
        auto& gs = GlobalState::instance();                                                                       \
        if (!gs.initialized)                                                                                      \
            return EXC_ERR_NOT_INITIALIZED;                                                                       \
    } while (0)

#define EXC_CHECK_NULL(ptr)                                                                                       \
    do {                                                                                                          \
        if (!(ptr))                                                                                               \
            return EXC_ERR_NULL_ARGUMENT;                                                                         \
    } while (0)

#define EXC_CHECK_NODE()                                                                                          \
    do {                                                                                                          \
        auto&           gs = GlobalState::instance();                                                             \
        std::lock_guard exc_state_lock(gs.mutex);                                                                 \
        if (!gs.initialized)                                                                                      \
            return EXC_ERR_NOT_INITIALIZED;                                                                       \
        if (!gs.node)                                                                                             \
            return EXC_ERR_NOT_INITIALIZED;                                                                       \
    } while (0)

} // namespace exc_ffi
