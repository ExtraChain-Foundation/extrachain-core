/*
 * ExtraChain Core — C FFI Bridge
 * Handle table instance, callback registry, event-to-callback routing.
 */

#include "exc_internal.h"

#include "core/extrachain_node.h"
#include "chain/dag.h"
#include "chain/transaction_cache.h"
#include "chain/transaction.h"
#include "dfs/dfs_service.h"
#include "network/network_service.h"
#include "chat/chat_manager.h"
#include "utils/bignumber.h"
#include "utils/exc_utils.h"

using namespace exc_ffi;

/* ── Memory management ──────────────────────────────────────────── */

extern "C" {

EXC_API void exc_string_free(char* str) {
    std::free(str);
}

EXC_API void exc_bytes_free(ExcBytes* bytes) {
    if (bytes) {
        std::free(bytes->data);
        bytes->data = nullptr;
        bytes->size = 0;
    }
}

EXC_API void exc_handle_free(ExcHandle handle) {
    if (handle != EXC_INVALID_HANDLE) {
        HandleTable::instance().release(handle);
    }
}

EXC_API void exc_mnemonic_free(ExcMnemonic* mnemonic) {
    if (mnemonic) {
        std::free(mnemonic->phrase);
        std::free(mnemonic->main_id);
        std::free(mnemonic->profile_id);
        mnemonic->phrase     = nullptr;
        mnemonic->main_id    = nullptr;
        mnemonic->profile_id = nullptr;
    }
}

EXC_API void exc_balance_list_free(ExcBalanceList* list) {
    if (list) {
        for (size_t i = 0; i < list->count; ++i) {
            std::free(list->entries[i].actor_id);
            std::free(list->entries[i].token_id);
            std::free(list->entries[i].amount);
        }
        std::free(list->entries);
        list->entries = nullptr;
        list->count   = 0;
    }
}

EXC_API void exc_string_list_free(ExcStringList* list) {
    if (list) {
        for (size_t i = 0; i < list->count; ++i) {
            std::free(list->items[i]);
        }
        std::free(list->items);
        list->items = nullptr;
        list->count = 0;
    }
}

} // extern "C"

/* ── Signal→Callback wiring ─────────────────────────────────────── */

namespace exc_ffi {

    void connect_events(ExtraChain::Core::ExtraChainNode* node) {
        auto&      cr                = CallbackRegistry::instance();
        auto&      event_connections = GlobalState::instance().event_connections;
        const auto connect           = [&event_connections](auto& event, auto callback) {
            event_connections.emplace_back(
                event.subscribe([callback = std::move(callback)](auto&&... args) mutable {
                    EventDispatchGuard guard;
                    callback(std::forward<decltype(args)>(args)...);
                }));
        };

        /* Node ready */
        connect(node->ready_event(), [&cr]() {
            cr.node_ready.invoke();
        });

        /* DAG sync */
        connect(node->dag()->sync_start_event(), [&cr](SectionId from, SectionId to) {
            auto s_from = from.to_string();
            auto s_to   = to.to_string();
            cr.dag_sync_start.invoke(s_from.c_str(), s_to.c_str());
        });

        connect(node->dag()->sync_progress_event(), [&cr](SectionId section) {
            auto s = section.to_string();
            cr.dag_sync_progress.invoke(s.c_str());
        });

        connect(node->dag()->sync_finish_event(), [&cr]() {
            cr.dag_sync_finish.invoke();
        });

        connect(node->dag()->status_event(), [&cr](DagStatus status) {
            cr.dag_status.invoke(static_cast<ExcDagStatus>(status));
        });

        /* Mining status (derived from DAG status changes) */
        connect(node->dag()->status_event(), [&cr, node](DagStatus /*status*/) {
            bool active = node->dag()->mode() == DagMode::Full;
            cr.mining_status.invoke(active);
        });

        /* DAG transactions */
        connect(node->dag()->transaction_sent_event(), [&cr](SectionId section_id, std::string hash) {
            auto s = section_id.to_string();
            cr.dag_tx_sended.invoke(s.c_str(), hash.c_str());
        });

        connect(node->dag()->transaction_approved_event(), [&cr](SectionId section_id, std::string hash) {
            auto s = section_id.to_string();
            cr.dag_tx_approved.invoke(s.c_str(), hash.c_str());
        });

        connect(node->dag()->transaction_rejected_event(), [&cr](SectionId section_id, std::string hash) {
            auto s = section_id.to_string();
            cr.dag_tx_not_approved.invoke(s.c_str(), hash.c_str());
        });

        connect(node->dag()->transaction_cache().self_transaction_event(),
                [&cr](const Transaction& tx, StatusTrx::StatusTrxType status) {
                    auto handle = HandleTable::instance().store(tx);
                    if (!cr.self_tx.invoke(handle, StatusTrx::toInt(status))) {
                        HandleTable::instance().release(handle);
                    }
                });

        /* Chat */
        connect(node->chat_manager()->message_added_event(),
                [&cr](ActorId owner_id, std::string file_id, Chat::Message msg) {
                    auto msg_json = Json::serialize(msg);
                    cr.chat_message.invoke(owner_id.to_string().c_str(), file_id.c_str(), msg_json.c_str());
                });

        connect(node->chat_manager()->chats_loaded_event(), [&cr]() {
            cr.chats_loaded.invoke();
        });

        connect(node->chat_manager()->chat_added_event(), [&cr](Chat::Chat chat) {
            auto chat_json = Json::serialize(chat);
            cr.chat_added.invoke(chat_json.c_str());
        });

        /* DFS */
        auto dfs = node->dfs();
        if (dfs) {
            connect(dfs->stored_event(), [&cr](ActorId owner_id, Dfs::DirRow dir_row) {
                auto json = Json::serialize(dir_row);
                cr.dfs_stored.invoke(owner_id.to_string().c_str(), json.c_str());
            });

            connect(dfs->downloaded_event(), [&cr](ActorId owner_id, Dfs::DirRow dir_row) {
                auto json = Json::serialize(dir_row);
                cr.dfs_downloaded.invoke(owner_id.to_string().c_str(), json.c_str());
            });

            connect(dfs->download_progress_event(), [&cr](ActorId owner_id, std::string file_id, int progress) {
                cr.dfs_download_progress.invoke(owner_id.to_string().c_str(), file_id.c_str(), progress);
            });
        }

        /* Network */
        auto net = node->network();
        if (net) {
            connect(net->connection_state_event(), [&cr](bool status, int count) {
                cr.connection_status.invoke(status);
                cr.connection_count.invoke(count);
            });
        }

        /* Actor rename */
        connect(node->actor_renamed_event(), [&cr](ActorId actor_id, std::string name) {
            cr.actor_renamed.invoke(actor_id.to_string().c_str(), name.c_str());
        });
    }

} // namespace exc_ffi

/* ── Callback registration (public API) ─────────────────────────── */

extern "C" {

EXC_API void exc_on_node_ready(ExcNodeReadyCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().node_ready.set(cb, user_data);
}

EXC_API void exc_on_dag_sync_start(ExcDagSyncStartCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dag_sync_start.set(cb, user_data);
}

EXC_API void exc_on_dag_sync_progress(ExcDagSyncProgressCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dag_sync_progress.set(cb, user_data);
}

EXC_API void exc_on_dag_sync_finished(ExcDagSyncFinishCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dag_sync_finish.set(cb, user_data);
}

EXC_API void exc_on_dag_status(ExcDagStatusCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dag_status.set(cb, user_data);
}

EXC_API void exc_on_dag_tx_sended(ExcDagTxSendedCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dag_tx_sended.set(cb, user_data);
}

EXC_API void exc_on_dag_tx_approved(ExcDagTxApprovedCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dag_tx_approved.set(cb, user_data);
}

EXC_API void exc_on_dag_tx_not_approved(ExcDagTxNotApprovedCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dag_tx_not_approved.set(cb, user_data);
}

EXC_API void exc_on_self_tx(ExcSelfTxCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().self_tx.set(cb, user_data);
}

EXC_API void exc_on_chat_message(ExcChatMessageCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().chat_message.set(cb, user_data);
}

EXC_API void exc_on_chats_loaded(ExcChatsLoadedCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().chats_loaded.set(cb, user_data);
}

EXC_API void exc_on_chat_added(ExcChatAddedCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().chat_added.set(cb, user_data);
}

EXC_API void exc_on_dfs_stored(ExcDfsStoredCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dfs_stored.set(cb, user_data);
}

EXC_API void exc_on_dfs_downloaded(ExcDfsDownloadedCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dfs_downloaded.set(cb, user_data);
}

EXC_API void exc_on_dfs_download_progress(ExcDfsDownloadProgressCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().dfs_download_progress.set(cb, user_data);
}

EXC_API void exc_on_connection_status(ExcConnectionStatusCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().connection_status.set(cb, user_data);
}

EXC_API void exc_on_connection_count(ExcConnectionCountCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().connection_count.set(cb, user_data);
}

EXC_API void exc_on_mining_status(ExcMiningStatusCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().mining_status.set(cb, user_data);
}

EXC_API void exc_on_actor_renamed(ExcActorRenamedCallback cb, ExcUserData user_data) {
    CallbackRegistry::instance().actor_renamed.set(cb, user_data);
}

} // extern "C"
