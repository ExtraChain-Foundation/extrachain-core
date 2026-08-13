/*
 * ExtraChain Core — C FFI Node Lifecycle
 * exc_init, exc_shutdown, exc_login, etc.
 */

#include "exc_internal.h"

#include "core/extrachain_node.h"
#include "network/network_service.h"
#include "extrachain_version.h"
#include "utils/exc_utils.h"

#include <filesystem>
#include <thread>

using namespace exc_ffi;

/* Forward declaration from exc_bridge.cpp */
namespace exc_ffi {
    void connect_events(ExtraChain::Core::ExtraChainNode* node);
}

namespace {
    ExcError initialize_node(uint16_t ws_port, bool main_thread_mode) {
        auto&        gs = GlobalState::instance();
        std::jthread previous_shutdown;
        {
            std::lock_guard lock(gs.mutex);
            if (gs.initialized || gs.initializing || gs.shutdown_in_progress) {
                return EXC_ERR_ALREADY_INITIALIZED;
            }
            gs.initializing         = true;
            gs.main_thread_mode     = main_thread_mode;
            gs.shutdown_requested   = false;
            gs.shutdown_in_progress = false;
            previous_shutdown       = std::move(gs.shutdown_worker);
        }
        if (previous_shutdown.joinable()) {
            previous_shutdown.join();
        }

        std::unique_ptr<ExtraChain::Core::ExtraChainNode> node;
        try {
            node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, ws_port ? ws_port : 17593);
            node->process();
            connect_events(node.get());
            node->start();
            std::lock_guard lock(gs.mutex);
            gs.node         = std::move(node);
            gs.initialized  = true;
            gs.initializing = false;
            return EXC_OK;
        } catch (...) {
            std::vector<boost::signals2::scoped_connection> event_connections;
            {
                std::lock_guard lock(gs.mutex);
                gs.initialized          = false;
                gs.initializing         = false;
                gs.shutdown_in_progress = false;
                event_connections       = std::move(gs.event_connections);
            }
            event_connections.clear();
            node.reset();
            return EXC_ERR_UNKNOWN;
        }
    }

} // anonymous namespace

/* ── Static version string ──────────────────────────────────────── */

/* ── Public API ──────────────────────────────────────────────────── */

extern "C" {

EXC_API ExcError exc_init(int argc, char** argv, uint16_t ws_port) {
    (void)argc;
    (void)argv;
    return initialize_node(ws_port, false);
}

EXC_API ExcError exc_init_main_thread(int argc, char** argv, uint16_t ws_port) {
    (void)argc;
    (void)argv;
    return initialize_node(ws_port, true);
}

EXC_API ExcError exc_run(void) {
    auto& gs = GlobalState::instance();
    std::unique_lock lock(gs.mutex);
    if (!gs.main_thread_mode)
        return EXC_ERR_INVALID_ARGUMENT;
    if (!gs.initialized)
        return EXC_ERR_NOT_INITIALIZED;
    gs.stopped.wait(lock, [&gs] {
        return gs.shutdown_requested;
    });

    return EXC_OK;
}

EXC_API bool exc_is_initialized(void) {
    return GlobalState::instance().initialized;
}

EXC_API const char* exc_version(void) {
    /* Real release version; extrachain_version is only a handshake compat anchor */
    return extrachain_node_version.c_str();
}

EXC_API uint32_t exc_api_version(void) {
    return EXC_C_API_VERSION;
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
        auto& gs  = GlobalState::instance();
        auto  res = gs.node->login(std::string(login), std::string(password));
        if (!res.has_value()) {
            switch (res.error()) {
            case LoadError::EmptyHash:
                result = EXC_ERR_ACCOUNT_EMPTY_HASH;
                break;
            case LoadError::NoProfiles:
                result = EXC_ERR_ACCOUNT_NO_PROFILES;
                break;
            case LoadError::NoAuthProfiles:
                result = EXC_ERR_ACCOUNT_NO_AUTH;
                break;
            case LoadError::Multiple:
                result = EXC_ERR_ACCOUNT_MULTIPLE;
                break;
            default:
                result = EXC_ERR_ACCOUNT_UNKNOWN;
                break;
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
        auto& gs  = GlobalState::instance();
        auto  res = gs.node->login(std::string(hash));
        if (!res.has_value()) {
            switch (res.error()) {
            case LoadError::EmptyHash:
                result = EXC_ERR_ACCOUNT_EMPTY_HASH;
                break;
            case LoadError::NoProfiles:
                result = EXC_ERR_ACCOUNT_NO_PROFILES;
                break;
            case LoadError::NoAuthProfiles:
                result = EXC_ERR_ACCOUNT_NO_AUTH;
                break;
            case LoadError::Multiple:
                result = EXC_ERR_ACCOUNT_MULTIPLE;
                break;
            default:
                result = EXC_ERR_ACCOUNT_UNKNOWN;
                break;
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
    auto&                                           gs = GlobalState::instance();
    std::vector<boost::signals2::scoped_connection> event_connections;
    {
        std::lock_guard lock(gs.mutex);
        if (!gs.initialized)
            return EXC_ERR_NOT_INITIALIZED;
        gs.initialized          = false;
        gs.shutdown_requested   = true;
        gs.shutdown_in_progress = true;
        event_connections       = std::move(gs.event_connections);
    }
    gs.stopped.notify_all();

    auto finish = [event_connections = std::move(event_connections)]() mutable {
        event_connections.clear();
        EventDispatchGuard::wait_until_idle();
        auto&                                             gs = GlobalState::instance();
        std::unique_ptr<ExtraChain::Core::ExtraChainNode> node;
        {
            std::unique_lock lock(gs.mutex);
            gs.calls_idle.wait(lock, [&gs] {
                return gs.active_calls == 0;
            });
            node = std::move(gs.node);
        }
        node->cleanUp();
        node.reset();
        std::lock_guard lock(gs.mutex);
        gs.shutdown_in_progress = false;
    };
    if (event_dispatch_depth > 0) {
        std::lock_guard lock(gs.mutex);
        gs.shutdown_worker = std::jthread(std::move(finish));
    } else {
        finish();
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
