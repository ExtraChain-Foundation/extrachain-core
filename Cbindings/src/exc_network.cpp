/*
 * ExtraChain Core — C FFI Network Operations
 */

#include "exc_internal.h"

#include "managers/extrachain_node.h"
#include "network/network_manager.h"

using namespace exc_ffi;

extern "C" {

EXC_API ExcError exc_network_connect(const char* ip) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(ip);

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* net = gs.node->network();
        emit net->connect_to_node(QString::fromStdString(std::string(ip)),
                                  Network::Protocol::WebSocket);
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_network_start(void) {
    EXC_CHECK_NODE();

    bool ok = dispatch_sync([]() {
        auto& gs = GlobalState::instance();
        gs.node->network()->connect_network();
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_network_is_connected(bool* out_connected) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_connected);

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        *out_connected = gs.node->network()->is_active_connection_exists();
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_network_connection_count(int* out_count) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_count);

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        *out_count = gs.node->network()->active_connections_count();
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_network_public_ip(char** out_ip) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_ip);

    ExcError result = EXC_OK;
    *out_ip = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        *out_ip = exc_strdup(gs.node->network()->public_ip());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_network_local_ip(char** out_ip) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_ip);

    ExcError result = EXC_OK;
    *out_ip = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        *out_ip = exc_strdup(gs.node->network()->local_ip().toStdString());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
