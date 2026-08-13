/*
 * ExtraChain Core — C FFI Janus Marketplace Operations
 */

#include "exc_internal.h"

#include "core/extrachain_node.h"
#include "managers/janus_manager.h"
#include "dfs/dfs_service.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

extern "C" {

EXC_API ExcError exc_janus_create_default_bid_template(const char* template_name) {
    EXC_CHECK_NODE();

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* jm = gs.node->janus_manager();

        std::string name = (template_name && template_name[0])
            ? std::string(template_name)
            : "JanusBids";

        bool success = jm->create_default_bid_template(name);
        if (!success) {
            result = EXC_ERR_UNKNOWN;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_janus_create_item_vector(const char* vector_name,
                                              const char* template_owner_id,
                                              const char* bid_template_name,
                                              char** out_dir_row_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(vector_name);
    EXC_CHECK_NULL(template_owner_id);
    EXC_CHECK_NULL(bid_template_name);
    EXC_CHECK_NULL(out_dir_row_json);

    ExcError result = EXC_OK;
    *out_dir_row_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* jm = gs.node->janus_manager();

        auto res = jm->create_item_vector(std::string(vector_name),
                                          ActorId(std::string(template_owner_id)),
                                          std::string(bid_template_name));
        if (!res.has_value()) {
            result = EXC_ERR_UNKNOWN;
            return;
        }
        *out_dir_row_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_janus_place_bid(const char* item_owner_id,
                                     const char* item_file_id,
                                     const char* bid_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(item_owner_id);
    EXC_CHECK_NULL(item_file_id);
    EXC_CHECK_NULL(bid_json);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* jm = gs.node->janus_manager();

        /* Parse bid_json into JanusBidBase — the universal bid struct */
        auto bid_result = Json::deserialize<JanusBidBase>(std::string(bid_json));
        if (!bid_result.has_value()) {
            result = EXC_ERR_JSON_ERROR;
            return;
        }
        JanusBidBase bid = bid_result.value();

        auto res = jm->place_bid<JanusBidBase>(ActorId(std::string(item_owner_id)),
                                               std::string(item_file_id),
                                               bid);
        if (!res.has_value()) {
            result = EXC_ERR_UNKNOWN;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
