/*
 * ExtraChain Core — C FFI Transactions & Balance & DAG Queries
 */

#include "exc_internal.h"

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "chain/dag.h"
#include "chain/dag_cache.h"
#include "chain/transaction.h"
#include "chain/actor_id.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

extern "C" {

/* ── Transactions ────────────────────────────────────────────────── */

EXC_API ExcError exc_transaction_send(const char* receiver, const char* amount,
                                      const char* token_id, ExcHandle* out_tx_handle) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(receiver);
    EXC_CHECK_NULL(amount);
    EXC_CHECK_NULL(out_tx_handle);

    ExcError result = EXC_OK;
    *out_tx_handle = EXC_INVALID_HANDLE;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();

        ActorId recv_id{std::string(receiver)};
        BigNumberFloat amt{std::string(amount), NumeralBase::Dec};
        ActorId token = (token_id && token_id[0]) ? ActorId(std::string(token_id)) : ActorId();

        auto res = gs.node->create_transaction(recv_id, amt, token);
        if (!res.has_value()) {
            switch (res.error()) {
            case TransactionError::NoSender:          result = EXC_ERR_TX_NO_SENDER; break;
            case TransactionError::EmptyTransaction:  result = EXC_ERR_TX_EMPTY; break;
            case TransactionError::NoLastSection:     result = EXC_ERR_TX_NO_LAST_SECTION; break;
            case TransactionError::InsufficientFunds: result = EXC_ERR_TX_INSUFFICIENT_FUNDS; break;
            case TransactionError::NoCurrentUser:     result = EXC_ERR_TX_NO_CURRENT_USER; break;
            case TransactionError::ZeroAmount:        result = EXC_ERR_TX_ZERO_AMOUNT; break;
            default:                                  result = EXC_ERR_TX_UNKNOWN; break;
            }
            return;
        }

        *out_tx_handle = HandleTable::instance().store(res.value());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_transaction_send_from(const char* sender, const char* receiver,
                                           const char* amount, const char* token_id,
                                           ExcHandle* out_tx_handle) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(sender);
    EXC_CHECK_NULL(receiver);
    EXC_CHECK_NULL(amount);
    EXC_CHECK_NULL(out_tx_handle);

    ExcError result = EXC_OK;
    *out_tx_handle = EXC_INVALID_HANDLE;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();

        ActorId send_id{std::string(sender)};
        ActorId recv_id{std::string(receiver)};
        BigNumberFloat amt{std::string(amount), NumeralBase::Dec};
        ActorId token = (token_id && token_id[0]) ? ActorId(std::string(token_id)) : ActorId();

        auto res = gs.node->create_transaction_from(send_id, recv_id, amt, token);
        if (!res.has_value()) {
            switch (res.error()) {
            case TransactionError::NoSender:          result = EXC_ERR_TX_NO_SENDER; break;
            case TransactionError::EmptyTransaction:  result = EXC_ERR_TX_EMPTY; break;
            case TransactionError::NoLastSection:     result = EXC_ERR_TX_NO_LAST_SECTION; break;
            case TransactionError::InsufficientFunds: result = EXC_ERR_TX_INSUFFICIENT_FUNDS; break;
            case TransactionError::NoCurrentUser:     result = EXC_ERR_TX_NO_CURRENT_USER; break;
            case TransactionError::ZeroAmount:        result = EXC_ERR_TX_ZERO_AMOUNT; break;
            default:                                  result = EXC_ERR_TX_UNKNOWN; break;
            }
            return;
        }

        *out_tx_handle = HandleTable::instance().store(res.value());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_balance_query(const char* actor_id, const char* token_id,
                                   char** out_balance) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(actor_id);
    EXC_CHECK_NULL(out_balance);

    ExcError result = EXC_OK;
    *out_balance = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dag = gs.node->dag();

        ActorId aid{std::string(actor_id)};
        TokenId tid = (token_id && token_id[0]) ? TokenId(std::string(token_id)) : TokenId();

        Balances balances = dag->calculate_actors_balance(std::vector<ActorId>{ aid });
        auto key = std::make_pair(aid, tid);
        auto it = balances.find(key);
        if (it != balances.end()) {
            *out_balance = exc_strdup(it->second.to_string(NumeralBase::Dec));
        } else {
            *out_balance = exc_strdup("0");
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_balance_query_all(const char** actor_ids, size_t count,
                                       ExcBalanceList* out_balances) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(actor_ids);
    EXC_CHECK_NULL(out_balances);

    ExcError result = EXC_OK;
    out_balances->entries = nullptr;
    out_balances->count = 0;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dag = gs.node->dag();

        std::vector<ActorId> ids;
        ids.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ids.emplace_back(std::string(actor_ids[i]));
        }

        Balances balances = dag->calculate_actors_balance(ids);

        if (balances.empty()) {
            out_balances->entries = nullptr;
            out_balances->count = 0;
            return;
        }

        out_balances->count = balances.size();
        out_balances->entries = static_cast<ExcBalanceEntry*>(
            std::malloc(sizeof(ExcBalanceEntry) * balances.size()));
        if (!out_balances->entries) {
            out_balances->count = 0;
            result = EXC_ERR_UNKNOWN;
            return;
        }

        size_t idx = 0;
        for (auto& [key, value] : balances) {
            out_balances->entries[idx].actor_id = exc_strdup(key.first.to_string());
            out_balances->entries[idx].token_id = exc_strdup(key.second.to_string());
            out_balances->entries[idx].amount = exc_strdup(value.to_string(NumeralBase::Dec));
            ++idx;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

/* ── Transaction accessors ──────────────────────────────────────── */

EXC_API ExcError exc_transaction_get_sender(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = exc_strdup(t->sender().to_string());
    return EXC_OK;
}

EXC_API ExcError exc_transaction_get_receiver(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = exc_strdup(t->receiver().to_string());
    return EXC_OK;
}

EXC_API ExcError exc_transaction_get_amount(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = exc_strdup(t->amount().to_string(NumeralBase::Dec));
    return EXC_OK;
}

EXC_API ExcError exc_transaction_get_hash(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = exc_strdup(t->hash());
    return EXC_OK;
}

EXC_API ExcError exc_transaction_get_section(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = exc_strdup(t->section().to_string());
    return EXC_OK;
}

EXC_API ExcError exc_transaction_get_type(ExcHandle tx, ExcTransactionType* out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = static_cast<ExcTransactionType>(t->type());
    return EXC_OK;
}

EXC_API ExcError exc_transaction_get_timestamp(ExcHandle tx, uint64_t* out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = t->timestamp();
    return EXC_OK;
}

EXC_API ExcError exc_transaction_get_token(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    auto* t = HandleTable::instance().get<Transaction>(tx);
    if (!t) return EXC_ERR_INVALID_HANDLE;
    *out = exc_strdup(t->token().to_string());
    return EXC_OK;
}

EXC_API void exc_transaction_free(ExcHandle tx) {
    if (tx != EXC_INVALID_HANDLE) {
        HandleTable::instance().release(tx);
    }
}

/* ── DAG queries ─────────────────────────────────────────────────── */

EXC_API ExcError exc_dag_current_section(char** out_section) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_section);

    ExcError result = EXC_OK;
    *out_section = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto sec = gs.node->dag()->current_section();
        *out_section = exc_strdup(sec.to_string());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dag_status(ExcDagStatus* out_status) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_status);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        *out_status = static_cast<ExcDagStatus>(gs.node->dag()->status());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dag_mode(ExcDagMode* out_mode) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_mode);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        *out_mode = static_cast<ExcDagMode>(gs.node->dag()->mode());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dag_read_section(const char* section_id, char** out_json) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(section_id);
    EXC_CHECK_NULL(out_json);

    ExcError result = EXC_OK;
    *out_json = nullptr;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        SectionId sid{std::string(section_id)};
        auto section = gs.node->dag()->read_section(sid);
        if (!section.has_value()) {
            result = EXC_ERR_DAG_SECTION_NOT_FOUND;
            return;
        }
        *out_json = exc_strdup(Json::serialize(section.value()));
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_dag_search_transaction(const char* hash, ExcHandle* out_tx) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(hash);
    EXC_CHECK_NULL(out_tx);

    ExcError result = EXC_OK;
    *out_tx = EXC_INVALID_HANDLE;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto tx = gs.node->dag()->search_duplicate_by_hash(std::string(hash));
        if (!tx.has_value()) {
            result = EXC_ERR_DAG_TX_NOT_FOUND;
            return;
        }
        *out_tx = HandleTable::instance().store(tx.value());
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API void exc_section_free(ExcHandle section) {
    if (section != EXC_INVALID_HANDLE) {
        HandleTable::instance().release(section);
    }
}

/* ── DAG mode ────────────────────────────────────────────────────── */

EXC_API ExcError exc_dag_set_mode(ExcDagMode mode) {
    EXC_CHECK_NODE();

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        auto* dag = gs.node->dag();
        if (mode == EXC_DAG_MODE_FULL)
            dag->force_full_mode();
        else
            dag->force_light_mode();
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

/* ── Mining ──────────────────────────────────────────────────────── */

EXC_API ExcError exc_mining_start(void) {
    EXC_CHECK_NODE();

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        gs.node->start_mining();
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_mining_stop(void) {
    EXC_CHECK_NODE();

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        gs.node->stop_mining();
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

EXC_API ExcError exc_mining_is_active(bool* out_active) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(out_active);

    ExcError result = EXC_OK;

    bool ok = dispatch_sync([&]() {
        auto& gs = GlobalState::instance();
        *out_active = gs.node->dag()->mode() == DagMode::Full;
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

} // extern "C"
