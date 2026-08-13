/*
 * ExtraChain Core — C FFI Chat Operations
 */

#include "exc_internal.h"

#include "core/extrachain_node.h"
#include "chat/chat_manager.h"
#include "chat/chat.h"
#include "chat/message.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

extern "C" {

EXC_API ExcError exc_chat_create_dialogue(const char* with_actor_id, char** out_chat_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(with_actor_id);
    EXC_CHECK_NULL(out_chat_json);

    ExcError result = EXC_OK;
    *out_chat_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* cm = gs.node->chat_manager();

        auto res = cm->create_dialogue(ActorId(std::string(with_actor_id)));
        if (!res.has_value()) {
            result = EXC_ERR_CHAT_UNKNOWN;
            return;
        }
        *out_chat_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_chat_create_myself(char** out_chat_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_chat_json);

    ExcError result = EXC_OK;
    *out_chat_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* cm = gs.node->chat_manager();

        auto res = cm->create_myself();
        if (!res.has_value()) {
            result = EXC_ERR_CHAT_UNKNOWN;
            return;
        }
        *out_chat_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_chat_list(char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* cm = gs.node->chat_manager();

        auto res = cm->read_chats();
        if (!res.has_value()) {
            result = EXC_ERR_CHAT_UNKNOWN;
            return;
        }
        *out_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_chat_send_text(const char* owner_id, const char* file_id,
                                    const char* text, const char* reply_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(text);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* cm = gs.node->chat_manager();

        Chat::MessageText msg_text;
        msg_text.text = std::string(text);
        if (reply_id && reply_id[0]) {
            msg_text.reply_id = std::string(reply_id);
        }

        auto res = cm->add_new_message_text(ActorId(std::string(owner_id)),
                                            std::string(file_id), msg_text);
        if (!res.has_value() || !res.value()) {
            result = EXC_ERR_CHAT_UNKNOWN;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_chat_read_messages(const char* owner_id, const char* file_id,
                                        char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* cm = gs.node->chat_manager();

        auto res = cm->read_chat_messages(ActorId(std::string(owner_id)),
                                          std::string(file_id));
        if (!res.has_value()) {
            result = EXC_ERR_CHAT_UNKNOWN;
            return;
        }
        *out_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_chat_read_last_message(const char* owner_id, const char* file_id,
                                            char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* cm = gs.node->chat_manager();

        auto res = cm->read_last_message(ActorId(std::string(owner_id)),
                                         std::string(file_id));
        if (!res.has_value()) {
            result = EXC_ERR_CHAT_UNKNOWN;
            return;
        }
        *out_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_chat_remove_message(const char* owner_id, const char* file_id,
                                         const char* message_id) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(owner_id);
    EXC_CHECK_NULL(file_id);
    EXC_CHECK_NULL(message_id);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* cm = gs.node->chat_manager();

        auto res = cm->remove_message(ActorId(std::string(owner_id)),
                                      std::string(file_id),
                                      std::string(message_id));
        if (!res.has_value() || !res.value()) {
            result = EXC_ERR_CHAT_UNKNOWN;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
