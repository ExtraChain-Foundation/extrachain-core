/*
 * ExtraChain Core — C FFI Node Lifecycle
 * exc_init, exc_shutdown, exc_login, etc.
 */

#include "exc_internal.h"

#include "managers/extrachain_node.h"
#include "network/network_manager.h"
#include "extrachain_version.h"
#include "utils/exc_utils.h"

#include <QCoreApplication>
#include <QThread>

using namespace exc_ffi;

/* Forward declaration from exc_bridge.cpp */
namespace exc_ffi {
void connect_signals(ExtraChainNode* node);
}

/* ── Background thread helper ────────────────────────────────────── */

namespace {

class EventLoopThread : public QThread {
public:
    EventLoopThread(int argc, char** argv, uint16_t ws_port)
        : argc_(argc), argv_(argv), ws_port_(ws_port) {}

    void run() override {
        auto& gs = GlobalState::instance();

        QCoreApplication app(argc_, argv_);
        gs.app = &app;

        auto wrapper = new ExtraChainNodeWrapper(nullptr, false, false, ws_port_);
        wrapper->init();
        gs.wrapper = wrapper;
        gs.node = wrapper->node;

        connect_signals(gs.node);
        gs.node->start();
        gs.initialized = true;

        ready_.store(true);

        app.exec();

        /* Cleanup after event loop exits */
        gs.initialized = false;
        gs.node = nullptr;
        delete gs.wrapper;
        gs.wrapper = nullptr;
        gs.app = nullptr;
    }

    bool is_ready() const { return ready_.load(); }

private:
    int              argc_;
    char**           argv_;
    uint16_t         ws_port_;
    std::atomic<bool> ready_ { false };
};

} // anonymous namespace

/* ── Static version string ──────────────────────────────────────── */

static const char* s_version = nullptr;

/* ── Public API ──────────────────────────────────────────────────── */

extern "C" {

EXC_API ExcError exc_init(int argc, char** argv, uint16_t ws_port) {
    auto& gs = GlobalState::instance();
    std::lock_guard lock(gs.mutex);

    if (gs.initialized) return EXC_ERR_ALREADY_INITIALIZED;

    gs.main_thread_mode = false;
    auto thread = new EventLoopThread(argc, argv, ws_port ? ws_port : 17593);
    gs.event_loop_thread = thread;
    thread->start();

    /* Wait for the event loop to be ready (timeout ~30s) */
    constexpr int max_wait_ms = 30000;
    int waited_ms = 0;
    while (!static_cast<EventLoopThread*>(thread)->is_ready()) {
        QThread::msleep(10);
        waited_ms += 10;
        if (waited_ms >= max_wait_ms) {
            return EXC_ERR_UNKNOWN;
        }
    }

    return EXC_OK;
}

EXC_API ExcError exc_init_main_thread(int argc, char** argv, uint16_t ws_port) {
    auto& gs = GlobalState::instance();
    std::lock_guard lock(gs.mutex);

    if (gs.initialized) return EXC_ERR_ALREADY_INITIALIZED;

    gs.main_thread_mode = true;

    /* Create QCoreApplication on the calling (main) thread */
    static QCoreApplication app(argc, argv);
    gs.app = &app;

    auto wrapper = new ExtraChainNodeWrapper(nullptr, false, false, ws_port ? ws_port : 17593);
    wrapper->init();
    gs.wrapper = wrapper;
    gs.node = wrapper->node;

    connect_signals(gs.node);
    gs.node->start();
    gs.initialized = true;

    return EXC_OK;
}

EXC_API ExcError exc_run(void) {
    auto& gs = GlobalState::instance();
    if (!gs.main_thread_mode) return EXC_ERR_INVALID_ARGUMENT;
    if (!gs.app) return EXC_ERR_NOT_INITIALIZED;

    gs.app->exec();

    /* Cleanup */
    gs.initialized = false;
    gs.node = nullptr;
    delete gs.wrapper;
    gs.wrapper = nullptr;
    gs.app = nullptr;

    return EXC_OK;
}

EXC_API bool exc_is_initialized(void) {
    return GlobalState::instance().initialized;
}

EXC_API const char* exc_version(void) {
    if (!s_version) {
        /* extrachain_version is a static std::string in extrachain_version.h */
        s_version = extrachain_version.c_str();
    }
    return s_version;
}

EXC_API ExcError exc_configure_logs(int log_type) {
    /* Maps to internal log configuration */
    (void)log_type;
    return EXC_OK;
}

EXC_API ExcError exc_login(const char* login, const char* password) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(login);
    EXC_CHECK_NULL(password);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto res = gs.node->login(std::string(login), std::string(password));
        if (!res.has_value()) {
            switch (res.error()) {
            case LoadError::EmptyHash:     result = EXC_ERR_ACCOUNT_EMPTY_HASH; break;
            case LoadError::NoProfiles:    result = EXC_ERR_ACCOUNT_NO_PROFILES; break;
            case LoadError::NoAuthProfiles: result = EXC_ERR_ACCOUNT_NO_AUTH; break;
            case LoadError::Multiple:      result = EXC_ERR_ACCOUNT_MULTIPLE; break;
            default:                       result = EXC_ERR_ACCOUNT_UNKNOWN; break;
            }
        } else {
            gs.node->network()->start_network();
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_login_hash(const char* hash) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(hash);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto res = gs.node->login(std::string(hash));
        if (!res.has_value()) {
            switch (res.error()) {
            case LoadError::EmptyHash:     result = EXC_ERR_ACCOUNT_EMPTY_HASH; break;
            case LoadError::NoProfiles:    result = EXC_ERR_ACCOUNT_NO_PROFILES; break;
            case LoadError::NoAuthProfiles: result = EXC_ERR_ACCOUNT_NO_AUTH; break;
            case LoadError::Multiple:      result = EXC_ERR_ACCOUNT_MULTIPLE; break;
            default:                       result = EXC_ERR_ACCOUNT_UNKNOWN; break;
            }
        } else {
            gs.node->network()->start_network();
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_logout(void) {
    EXC_CHECK_NODE();

    bool ok = dispatch_sync([&]() {
        GlobalState::instance().node->logout();
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_shutdown(void) {
    auto& gs = GlobalState::instance();
    if (!gs.initialized) return EXC_ERR_NOT_INITIALIZED;

    if (gs.main_thread_mode) {
        /* In main-thread mode, quit the event loop. exc_run() will clean up. */
        if (gs.app) {
            gs.app->quit();
        }
    } else {
        /* Background-thread mode: quit app and wait for thread */
        dispatch_sync([]() {
            auto& gs = GlobalState::instance();
            if (gs.app) gs.app->quit();
        });

        if (gs.event_loop_thread) {
            gs.event_loop_thread->wait();
            delete gs.event_loop_thread;
            gs.event_loop_thread = nullptr;
        }
    }

    return EXC_OK;
}

EXC_API ExcError exc_wipe_data(void) {
    EXC_CHECK_NODE();

    /* Wipe is a destructive operation — remove the data directories */
    bool ok = dispatch_sync([]() {
        std::filesystem::remove_all("blockchain");
        std::filesystem::remove_all("dfs");
        std::filesystem::remove_all("keystore");
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
