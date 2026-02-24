/*
 * ExtraChain Core — C FFI Token Management
 */

#include "exc_internal.h"

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "managers/token_manager.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

extern "C" {

EXC_API ExcError exc_token_create(const char* name, const char* ticker,
                                  const char* amount, const char* color,
                                  char** out_token_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(name);
    EXC_CHECK_NULL(ticker);
    EXC_CHECK_NULL(amount);
    EXC_CHECK_NULL(color);
    EXC_CHECK_NULL(out_token_json);

    ExcError result = EXC_OK;
    *out_token_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* tm = gs.node->token_manager();
        auto* ac = gs.node->account_controller();

        ActorId owner = ac->current_wallet().id();
        BigNumberFloat amt(std::string(amount), NumeralBase::Dec);

        auto res = tm->create_token(owner, std::string(name), std::string(ticker),
                                    amt, std::string(color));
        if (!res.has_value()) {
            switch (res.error()) {
            case CreateTokenError::NoConnections: result = EXC_ERR_TOKEN_NO_CONNECTIONS; break;
            case CreateTokenError::InvalidAmount: result = EXC_ERR_TOKEN_INVALID_AMOUNT; break;
            case CreateTokenError::InvalidName:   result = EXC_ERR_TOKEN_INVALID_NAME; break;
            case CreateTokenError::ExistToken:    result = EXC_ERR_TOKEN_EXISTS; break;
            case CreateTokenError::InvalidTx:     result = EXC_ERR_TOKEN_INVALID_TX; break;
            case CreateTokenError::InvalidOwnerId: result = EXC_ERR_TOKEN_INVALID_OWNER; break;
            default:                              result = EXC_ERR_UNKNOWN; break;
            }
            return;
        }
        *out_token_json = exc_strdup(Json::serialize(res.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_token_exists(const char* name, const char* ticker, bool* out_exists) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(name);
    EXC_CHECK_NULL(ticker);
    EXC_CHECK_NULL(out_exists);

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* tm = gs.node->token_manager();
        *out_exists = tm->token_exists(std::string(name), std::string(ticker));
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_token_list(char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto tokens = TokenManager::read_tokens();
        *out_json = exc_strdup(Json::serialize(tokens));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
