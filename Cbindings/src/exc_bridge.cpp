/*
 * ExtraChain Core — C FFI Bridge
 * Handle table instance, callback registry, signal→callback routing.
 */

#include "exc_internal.h"

#include "managers/extrachain_node.h"
#include "chain/dag.h"
#include "chain/transaction.h"
#include "dfs/dfs_controller.h"
#include "network/network_manager.h"
#include "chat/chat_manager.h"
#include "utils/exc_utils.h"
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
        mnemonic->phrase = nullptr;
        mnemonic->main_id = nullptr;
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
        list->count = 0;
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

void connect_signals(ExtraChainNode* node) {
    auto& cr = CallbackRegistry::instance();

    /* Node ready */
    QObject::connect(node, &ExtraChainNode::ready, [&cr]() {
        if (cr.node_ready.callback)
            cr.node_ready.callback(cr.node_ready.user_data);
    });

    /* DAG sync */
    QObject::connect(node, &ExtraChainNode::dagSyncStart,
        [&cr](SectionId from, SectionId to) {
            if (cr.dag_sync_start.callback) {
                auto s_from = from.to_string();
                auto s_to = to.to_string();
                cr.dag_sync_start.callback(s_from.c_str(), s_to.c_str(),
                                           cr.dag_sync_start.user_data);
            }
        });

    QObject::connect(node, &ExtraChainNode::dagSyncProgress,
        [&cr](SectionId section) {
            if (cr.dag_sync_progress.callback) {
                auto s = section.to_string();
                cr.dag_sync_progress.callback(s.c_str(), cr.dag_sync_progress.user_data);
            }
        });

    QObject::connect(node, &ExtraChainNode::dagSyncFinish,
        [&cr]() {
            if (cr.dag_sync_finish.callback)
                cr.dag_sync_finish.callback(cr.dag_sync_finish.user_data);
        });

    QObject::connect(node, &ExtraChainNode::dagStatus,
        [&cr](DagStatus status) {
            if (cr.dag_status.callback)
                cr.dag_status.callback(static_cast<ExcDagStatus>(status),
                                       cr.dag_status.user_data);
        });

    /* Mining status (derived from DAG status changes) */
    QObject::connect(node, &ExtraChainNode::dagStatus,
        [&cr, node](DagStatus /*status*/) {
            if (cr.mining_status.callback) {
                bool active = node->dag()->mode() == DagMode::Full;
                cr.mining_status.callback(active, cr.mining_status.user_data);
            }
        });

    /* DAG transactions */
    QObject::connect(node, &ExtraChainNode::dagTxSended,
        [&cr](SectionId section_id, std::string hash) {
            if (cr.dag_tx_sended.callback) {
                auto s = section_id.to_string();
                cr.dag_tx_sended.callback(s.c_str(), hash.c_str(),
                                          cr.dag_tx_sended.user_data);
            }
        });

    QObject::connect(node, &ExtraChainNode::dagTxApproved,
        [&cr](SectionId section_id, std::string hash) {
            if (cr.dag_tx_approved.callback) {
                auto s = section_id.to_string();
                cr.dag_tx_approved.callback(s.c_str(), hash.c_str(),
                                            cr.dag_tx_approved.user_data);
            }
        });

    QObject::connect(node, &ExtraChainNode::dagTxNotApproved,
        [&cr](SectionId section_id, std::string hash) {
            if (cr.dag_tx_not_approved.callback) {
                auto s = section_id.to_string();
                cr.dag_tx_not_approved.callback(s.c_str(), hash.c_str(),
                                                cr.dag_tx_not_approved.user_data);
            }
        });

    QObject::connect(node, &ExtraChainNode::selfTxAdded,
        [&cr](const Transaction& tx, StatusTrx::StatusTrxType status) {
            if (cr.self_tx.callback) {
                auto handle = HandleTable::instance().store(tx);
                cr.self_tx.callback(handle, StatusTrx::toInt(status),
                                    cr.self_tx.user_data);
            }
        });

    /* Chat */
    QObject::connect(node, &ExtraChainNode::messageAdded,
        [&cr](ActorId owner_id, std::string file_id, Chat::Message msg) {
            if (cr.chat_message.callback) {
                auto msg_json = Json::serialize(msg);
                cr.chat_message.callback(owner_id.to_string().c_str(),
                                         file_id.c_str(),
                                         msg_json.c_str(),
                                         cr.chat_message.user_data);
            }
        });

    QObject::connect(node, &ExtraChainNode::chatsLoaded,
        [&cr]() {
            if (cr.chats_loaded.callback)
                cr.chats_loaded.callback(cr.chats_loaded.user_data);
        });

    QObject::connect(node, &ExtraChainNode::chatAdded,
        [&cr](Chat::Chat chat) {
            if (cr.chat_added.callback) {
                auto chat_json = Json::serialize(chat);
                cr.chat_added.callback(chat_json.c_str(), cr.chat_added.user_data);
            }
        });

    /* DFS */
    auto dfs = node->dfs();
    if (dfs) {
        QObject::connect(dfs, &DfsController::stored,
            [&cr](ActorId owner_id, Dfs::DirRow dir_row) {
                if (cr.dfs_stored.callback) {
                    auto json = Json::serialize(dir_row);
                    cr.dfs_stored.callback(owner_id.to_string().c_str(),
                                           json.c_str(),
                                           cr.dfs_stored.user_data);
                }
            });

        QObject::connect(dfs, &DfsController::downloaded,
            [&cr](ActorId owner_id, Dfs::DirRow dir_row) {
                if (cr.dfs_downloaded.callback) {
                    auto json = Json::serialize(dir_row);
                    cr.dfs_downloaded.callback(owner_id.to_string().c_str(),
                                               json.c_str(),
                                               cr.dfs_downloaded.user_data);
                }
            });

        QObject::connect(dfs, &DfsController::downloadProgress,
            [&cr](ActorId owner_id, std::string file_id, int progress) {
                if (cr.dfs_download_progress.callback)
                    cr.dfs_download_progress.callback(owner_id.to_string().c_str(),
                                                       file_id.c_str(),
                                                       progress,
                                                       cr.dfs_download_progress.user_data);
            });
    }

    /* Network */
    auto net = node->network();
    if (net) {
        QObject::connect(net, &NetworkManager::connectionStatusChanged,
            [&cr](bool status) {
                if (cr.connection_status.callback)
                    cr.connection_status.callback(status, cr.connection_status.user_data);
            });

        QObject::connect(net, &NetworkManager::connectionsCountChanged,
            [&cr](int count) {
                if (cr.connection_count.callback)
                    cr.connection_count.callback(count, cr.connection_count.user_data);
            });
    }

    /* Actor rename */
    QObject::connect(node, &ExtraChainNode::actorRenamed,
        [&cr](ActorId actor_id, std::string name) {
            if (cr.actor_renamed.callback)
                cr.actor_renamed.callback(actor_id.to_string().c_str(),
                                          name.c_str(),
                                          cr.actor_renamed.user_data);
        });
}

} // namespace exc_ffi

/* ── Callback registration (public API) ─────────────────────────── */

extern "C" {

EXC_API void exc_on_node_ready(ExcNodeReadyCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().node_ready;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dag_sync_start(ExcDagSyncStartCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dag_sync_start;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dag_sync_progress(ExcDagSyncProgressCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dag_sync_progress;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dag_sync_finished(ExcDagSyncFinishCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dag_sync_finish;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dag_status(ExcDagStatusCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dag_status;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dag_tx_sended(ExcDagTxSendedCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dag_tx_sended;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dag_tx_approved(ExcDagTxApprovedCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dag_tx_approved;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dag_tx_not_approved(ExcDagTxNotApprovedCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dag_tx_not_approved;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_self_tx(ExcSelfTxCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().self_tx;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_chat_message(ExcChatMessageCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().chat_message;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_chats_loaded(ExcChatsLoadedCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().chats_loaded;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_chat_added(ExcChatAddedCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().chat_added;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dfs_stored(ExcDfsStoredCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dfs_stored;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dfs_downloaded(ExcDfsDownloadedCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dfs_downloaded;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_dfs_download_progress(ExcDfsDownloadProgressCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().dfs_download_progress;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_connection_status(ExcConnectionStatusCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().connection_status;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_connection_count(ExcConnectionCountCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().connection_count;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_mining_status(ExcMiningStatusCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().mining_status;
    s.callback = cb; s.user_data = user_data;
}

EXC_API void exc_on_actor_renamed(ExcActorRenamedCallback cb, ExcUserData user_data) {
    auto& s = CallbackRegistry::instance().actor_renamed;
    s.callback = cb; s.user_data = user_data;
}

} // extern "C"
