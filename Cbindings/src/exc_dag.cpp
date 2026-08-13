/*
 * ExtraChain Core — C FFI Transactions & Balance & DAG Queries
 */

#include "exc_internal.h"

#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "chain/dag.h"
#include "chain/dag_cache.h"
#include "chain/transaction.h"
#include "chain/actor_id.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

namespace {

template <typename Getter>
ExcError transaction_string(ExcHandle transaction, char** output, Getter&& getter) {
    *output = nullptr;
    std::string value;
    const bool found = HandleTable::instance().with<Transaction>(transaction, [&](const Transaction& current) {
        value = std::invoke(std::forward<Getter>(getter), current);
    });
    if (!found) {
        return EXC_ERR_INVALID_HANDLE;
    }

    *output = exc_strdup(value);
    return *output != nullptr ? EXC_OK : EXC_ERR_UNKNOWN;
}

} // namespace

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
        BigNumberFloat amt{std::string(amount)};
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
        BigNumberFloat amt{std::string(amount)};
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
            *out_balance = exc_strdup(it->second.to_string());
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
            out_balances->entries[idx].amount = exc_strdup(value.to_string());
            ++idx;
        }
    });

    return ok ? result : EXC_ERR_DISPATCH_FAILED;
}

/* ── Transaction accessors ──────────────────────────────────────── */

EXC_API ExcError exc_transaction_get_sender(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    return transaction_string(tx, out, [](const Transaction& value) {
        return value.sender().to_string();
    });
}

EXC_API ExcError exc_transaction_get_receiver(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    return transaction_string(tx, out, [](const Transaction& value) {
        return value.receiver().to_string();
    });
}

EXC_API ExcError exc_transaction_get_amount(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    return transaction_string(tx, out, [](const Transaction& value) {
        return value.amount().to_string();
    });
}

EXC_API ExcError exc_transaction_get_hash(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    return transaction_string(tx, out, [](const Transaction& value) {
        return value.hash();
    });
}

EXC_API ExcError exc_transaction_get_section(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    return transaction_string(tx, out, [](const Transaction& value) {
        return value.section().to_string();
    });
}

EXC_API ExcError exc_transaction_get_type(ExcHandle tx, ExcTransactionType* out) {
    EXC_CHECK_NULL(out);
    const bool found = HandleTable::instance().with<Transaction>(tx, [&](const Transaction& value) {
        *out = static_cast<ExcTransactionType>(value.type());
    });
    return found ? EXC_OK : EXC_ERR_INVALID_HANDLE;
}

EXC_API ExcError exc_transaction_get_timestamp(ExcHandle tx, uint64_t* out) {
    EXC_CHECK_NULL(out);
    const bool found = HandleTable::instance().with<Transaction>(tx, [&](const Transaction& value) {
        *out = value.timestamp();
    });
    return found ? EXC_OK : EXC_ERR_INVALID_HANDLE;
}

EXC_API ExcError exc_transaction_get_token(ExcHandle tx, char** out) {
    EXC_CHECK_NULL(out);
    return transaction_string(tx, out, [](const Transaction& value) {
        return value.token().to_string();
    });
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

EXC_API ExcError exc_dag_find_transaction(const char* section_id,
                                          const char* hash,
                                          ExcHandle*  out_tx) {
    EXC_CHECK_NODE();
    EXC_CHECK_NULL(section_id);
    EXC_CHECK_NULL(hash);
    EXC_CHECK_NULL(out_tx);

    ExcError result = EXC_OK;
    *out_tx = EXC_INVALID_HANDLE;

    bool ok = dispatch_sync([&]() {
        auto& gs        = GlobalState::instance();
        auto  sid       = SectionId::create(std::string(section_id));
        if (!sid.has_value()) {
            result = EXC_ERR_DAG_TX_NOT_FOUND;
            return;
        }
        auto tx = gs.node->dag()->find_transaction(sid.value(), std::string(hash));
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
