/*
 * ExtraChain Core — C FFI Internal Helpers
 * NOT part of the public API. Do not include from consumer code.
 */

#pragma once

#include "../extrachain_c.h"

#include <any>
#include <atomic>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

/* Forward declarations of ExtraChain types */
class ExtraChainNode;
class ExtraChainNodeWrapper;

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
    if (!s) return nullptr;
    size_t len = std::strlen(s);
    char* p = static_cast<char*>(std::malloc(len + 1));
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
        ExcHandle h = next_++;
        entries_[h] = Entry { std::any(std::forward<T>(value)), std::type_index(typeid(std::decay_t<T>)) };
        return h;
    }

    template <typename T>
    T* get(ExcHandle h) {
        std::lock_guard lock(mutex_);
        auto it = entries_.find(h);
        if (it == entries_.end()) return nullptr;
        if (it->second.type != std::type_index(typeid(T))) return nullptr;
        return std::any_cast<T>(&it->second.value);
    }

    void release(ExcHandle h) {
        std::lock_guard lock(mutex_);
        entries_.erase(h);
    }

private:
    HandleTable() : next_(1) {}

    struct Entry {
        std::any        value;
        std::type_index type{typeid(void)};
    };

    std::mutex                               mutex_;
    std::atomic<ExcHandle>                   next_;
    std::unordered_map<ExcHandle, Entry>     entries_;
};

/* ── Global state ────────────────────────────────────────────────── */

struct GlobalState {
    std::mutex                   mutex;
    QCoreApplication*            app = nullptr;
    ExtraChainNodeWrapper*       wrapper = nullptr;
    ExtraChainNode*              node = nullptr;
    QThread*                     event_loop_thread = nullptr;
    bool                         initialized = false;
    bool                         main_thread_mode = false;

    static GlobalState& instance() {
        static GlobalState gs;
        return gs;
    }
};

/* ── Dispatch helpers ────────────────────────────────────────────── */

/*
 * Run a function on the Qt thread synchronously.
 * If already on the Qt thread, runs directly.
 * Returns false if dispatch failed.
 */
inline bool dispatch_sync(std::function<void()> fn) {
    auto& gs = GlobalState::instance();
    if (!gs.app) return false;

    if (QThread::currentThread() == gs.app->thread()) {
        fn();
        return true;
    }

    return QMetaObject::invokeMethod(
        gs.app,
        std::move(fn),
        Qt::BlockingQueuedConnection);
}

/*
 * Run a function on the Qt thread asynchronously.
 * Returns false if dispatch failed.
 */
inline bool dispatch_async(std::function<void()> fn) {
    auto& gs = GlobalState::instance();
    if (!gs.app) return false;

    return QMetaObject::invokeMethod(
        gs.app,
        std::move(fn),
        Qt::QueuedConnection);
}

/* ── Callback registry ──────────────────────────────────────────── */

template <typename CallbackType>
struct CallbackSlot {
    CallbackType callback = nullptr;
    ExcUserData  user_data = nullptr;
};

struct CallbackRegistry {
    CallbackSlot<ExcNodeReadyCallback>          node_ready;

    CallbackSlot<ExcDagSyncStartCallback>       dag_sync_start;
    CallbackSlot<ExcDagSyncProgressCallback>    dag_sync_progress;
    CallbackSlot<ExcDagSyncFinishCallback>      dag_sync_finish;
    CallbackSlot<ExcDagStatusCallback>          dag_status;

    CallbackSlot<ExcDagTxSendedCallback>        dag_tx_sended;
    CallbackSlot<ExcDagTxApprovedCallback>      dag_tx_approved;
    CallbackSlot<ExcDagTxNotApprovedCallback>   dag_tx_not_approved;
    CallbackSlot<ExcSelfTxCallback>             self_tx;

    CallbackSlot<ExcChatMessageCallback>        chat_message;
    CallbackSlot<ExcChatsLoadedCallback>        chats_loaded;
    CallbackSlot<ExcChatAddedCallback>          chat_added;

    CallbackSlot<ExcDfsStoredCallback>          dfs_stored;
    CallbackSlot<ExcDfsDownloadedCallback>      dfs_downloaded;
    CallbackSlot<ExcDfsDownloadProgressCallback> dfs_download_progress;

    CallbackSlot<ExcConnectionStatusCallback>   connection_status;
    CallbackSlot<ExcConnectionCountCallback>    connection_count;

    CallbackSlot<ExcActorRenamedCallback>       actor_renamed;

    static CallbackRegistry& instance() {
        static CallbackRegistry cr;
        return cr;
    }
};

/* ── Convenience macros ──────────────────────────────────────────── */

#define EXC_CHECK_INIT()                          \
    do {                                          \
        auto& gs = GlobalState::instance();       \
        if (!gs.initialized) return EXC_ERR_NOT_INITIALIZED; \
    } while (0)

#define EXC_CHECK_NULL(ptr)                       \
    do {                                          \
        if (!(ptr)) return EXC_ERR_NULL_ARGUMENT; \
    } while (0)

#define EXC_CHECK_NODE()                          \
    do {                                          \
        auto& gs = GlobalState::instance();       \
        if (!gs.initialized) return EXC_ERR_NOT_INITIALIZED; \
        if (!gs.node) return EXC_ERR_NOT_INITIALIZED;        \
    } while (0)

} // namespace exc_ffi
