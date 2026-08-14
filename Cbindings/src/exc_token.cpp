/*
 * ExtraChain Core — C FFI Token Management
 */

#include "exc_internal.h"

#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "managers/token_manager.h"
#include "contracts/toolchain_registry.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

#include <unordered_map>

using namespace exc_ffi;

namespace {
    ExcError token_error(CreateTokenError error) {
        switch (error) {
        case CreateTokenError::NoConnections:
            return EXC_ERR_TOKEN_NO_CONNECTIONS;
        case CreateTokenError::InvalidAmount:
            return EXC_ERR_TOKEN_INVALID_AMOUNT;
        case CreateTokenError::InvalidName:
            return EXC_ERR_TOKEN_INVALID_NAME;
        case CreateTokenError::ExistToken:
            return EXC_ERR_TOKEN_EXISTS;
        case CreateTokenError::InvalidTx:
            return EXC_ERR_TOKEN_INVALID_TX;
        case CreateTokenError::InvalidOwnerId:
            return EXC_ERR_TOKEN_INVALID_OWNER;
        }
        return EXC_ERR_UNKNOWN;
    }
} // namespace

extern "C" {

EXC_API ExcError exc_token_create(const char* name,
                                  const char* ticker,
                                  const char* amount,
                                  uint8_t     decimals,
                                  const char* color,
                                  char**      out_token_json) {
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

        ActorId        owner = ac->current_wallet().id();
        BigNumberFloat amt { std::string(amount) };

        auto res =
            tm->create_token(owner, std::string(name), std::string(ticker), amt, std::string(color), "", decimals);
        if (!res.has_value()) {
            result = token_error(res.error());
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
        auto& gs    = GlobalState::instance();
        auto* tm    = gs.node->token_manager();
        *out_exists = tm->token_exists(std::string(name), std::string(ticker));
    });

    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_token_list(char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json       = nullptr;

    bool ok = dispatch_sync([&]() {
        auto tokens = GlobalState::instance().node->token_manager()->list_tokens();
        std::unordered_map<ActorId, std::string> names;
        names.reserve(tokens.size());
        for (const auto& token : tokens) {
            names.insert_or_assign(token.token_id, token.name);
        }
        *out_json = exc_strdup(Json::serialize(names));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_token_legacy_list(char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_json);

    *out_json = nullptr;
    bool ok   = dispatch_sync([&]() {
        auto tokens = GlobalState::instance().node->token_manager()->legacy_tokens();
        *out_json   = exc_strdup(Json::serialize(tokens));
    });
    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_token_migration_publish_target(const char* token_id,
                                                    const char* language,
                                                    char**      out_transaction_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(token_id);
    EXC_CHECK_NULL(language);
    EXC_CHECK_NULL(out_transaction_json);

    ExcError result = EXC_OK;
    *out_transaction_json = nullptr;
    bool ok               = dispatch_sync([&]() {
        auto parsed = TokenId::create(std::string(token_id));
        auto parsed_language = ExtraChain::Contracts::toolchain_language(language);
        if (!parsed.has_value() || parsed->is_zero() || !parsed_language.has_value()) {
            result = EXC_ERR_TOKEN_INVALID_TX;
            return;
        }
        auto published =
            GlobalState::instance().node->token_manager()->publish_legacy_token_target(*parsed, *parsed_language);
        if (!published.has_value()) {
            result = token_error(published.error());
            return;
        }
        *out_transaction_json = exc_strdup(Json::serialize(*published));
    });
    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_token_migration_link(const char* token_id,
                                          const char* target_contract_id,
                                          char**      out_transaction_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(token_id);
    EXC_CHECK_NULL(target_contract_id);
    EXC_CHECK_NULL(out_transaction_json);

    ExcError result       = EXC_OK;
    *out_transaction_json = nullptr;
    bool ok               = dispatch_sync([&]() {
        const auto token  = TokenId::create(std::string(token_id));
        const auto target = ActorId::create(std::string(target_contract_id));
        if (!token.has_value() || token->is_zero() || !target.has_value() || target->is_zero()) {
            result = EXC_ERR_TOKEN_INVALID_TX;
            return;
        }
        const auto linked = GlobalState::instance().node->token_manager()->link_legacy_token(*token, *target);
        if (!linked.has_value()) {
            result = token_error(linked.error());
            return;
        }
        *out_transaction_json = exc_strdup(Json::serialize(*linked));
    });
    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_token_migration_status(char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_json);

    *out_json = nullptr;
    bool ok   = dispatch_sync([&]() {
        *out_json =
            exc_strdup(Json::serialize(GlobalState::instance().node->token_manager()->migration_statuses()));
    });
    return ok ? EXC_OK : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
