/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "managers/extrachain_node.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QNetworkInformation>
#include <QPointer>

#include <boost/asio/post.hpp>

#include "chain/dag.h"
#include "chat/chat_manager.h"
#include "utils/exc_logs.h"

namespace {
    template <typename Callback>
    void queue_for_qt(const QPointer<ExtraChainNode>& node, Callback&& callback) {
        if (node) {
            QMetaObject::invokeMethod(node, std::forward<Callback>(callback), Qt::QueuedConnection);
        }
    }
} // namespace

ExtraChainNode::ExtraChainNode(bool                          is_client_application,
                               bool                          is_custom_app,
                               std::uint16_t                 port,
                               std::optional<RuntimeProfile> runtime_profile)
    : QObject(nullptr)
    , ExtraChain::Core::ExtraChainNode(is_client_application,
                                       is_custom_app,
                                       port,
                                       runtime_profile,
                                       QCoreApplication::applicationVersion().toStdString()) {
    QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
    bridge_node_events();
}

ExtraChainNode::~ExtraChainNode() {
    qt_connections_.clear();
    ExtraChain::Core::ExtraChainNode::cleanUp();
}

void ExtraChainNode::process() {
    ExtraChain::Core::ExtraChainNode::process();
    bridge_service_events();
}

void ExtraChainNode::cleanUp() {
    qt_connections_.clear();
    ExtraChain::Core::ExtraChainNode::cleanUp();
}

NetworkManager* ExtraChainNode::network() const {
    return static_cast<NetworkManager*>(ExtraChain::Core::ExtraChainNode::network());
}

DfsController* ExtraChainNode::dfs() const {
    return static_cast<DfsController*>(ExtraChain::Core::ExtraChainNode::dfs());
}

std::pair<QString, QString> ExtraChainNode::init_public_ip_and_country() const {
    const auto& [ip, country] = public_ip_and_country_value();
    return { QString::fromStdString(ip), QString::fromStdString(country) };
}

void ExtraChainNode::notificationToken(QString os, QString actor_id, QString token) {
    notification_token(os.toStdString(), actor_id.toStdString(), token.toStdString());
}

DfsService* ExtraChainNode::create_dfs_service() {
    return new DfsController(this, this);
}

NetworkService* ExtraChainNode::create_network_service() {
    return new NetworkManager(this, network_runtime(), configured_port(), this);
}

void ExtraChainNode::bridge_node_events() {
    const QPointer<ExtraChainNode> node(this);
    qt_connections_.emplace_back(initialized_event().subscribe([node] {
        queue_for_qt(node, [node] {
            if (node)
                emit node->nodeInitialised();
        });
    }));
    qt_connections_.emplace_back(ready_event().subscribe([node] {
        queue_for_qt(node, [node] {
            if (node)
                emit node->ready();
        });
    }));
    qt_connections_.emplace_back(runtime_activity_event().subscribe([node](RuntimeActivity activity) {
        queue_for_qt(node, [node, activity] {
            if (node)
                emit node->runtimeActivityChanged(activity);
        });
    }));
    qt_connections_.emplace_back(
        actor_renamed_event().subscribe([node](const ActorId& actor_id, const std::string& name) {
            queue_for_qt(node, [node, actor_id, name] {
                if (node)
                    emit node->actorRenamed(actor_id, name);
            });
        }));
    qt_connections_.emplace_back(actor_renames_loaded_event().subscribe([node] {
        queue_for_qt(node, [node] {
            if (node)
                emit node->actorRenamedLoaded();
        });
    }));
    qt_connections_.emplace_back(
        subscription_added_event().subscribe([node](const ActorId& owner_id, const std::string& file_id) {
            queue_for_qt(node, [node, owner_id, file_id] {
                if (node)
                    emit node->subscriptionAdded(owner_id, file_id);
            });
        }));
}

void ExtraChainNode::bridge_service_events() {
    auto* dag_service  = dag();
    auto* chat_service = chat_manager();
    if (!dag_service || !chat_service) {
        return;
    }

    const QPointer<ExtraChainNode> node(this);
    const auto                     queue = [node](auto callback) {
        queue_for_qt(node, std::move(callback));
    };

    qt_connections_.emplace_back(dag_service->status_event().subscribe([node, queue](DagStatus status) {
        queue([node, status] {
            if (node)
                emit node->dagStatus(status);
        });
    }));
    qt_connections_.emplace_back(
        dag_service->sync_start_event().subscribe([node, queue](SectionId from, SectionId to) {
            queue([node, from, to] {
                if (node)
                    emit node->dagSyncStart(from, to);
            });
        }));
    qt_connections_.emplace_back(dag_service->sync_progress_event().subscribe([node, queue](SectionId section) {
        queue([node, section] {
            if (node)
                emit node->dagSyncProgress(section);
        });
    }));
    qt_connections_.emplace_back(dag_service->sync_finish_event().subscribe([node, queue] {
        queue([node] {
            if (node)
                emit node->dagSyncFinish();
        });
    }));
    qt_connections_.emplace_back(dag_service->timer_start_event().subscribe([node, queue](int delay_ms) {
        queue([node, delay_ms] {
            if (node)
                emit node->dagTimerStart(delay_ms);
        });
    }));
    qt_connections_.emplace_back(dag_service->timer_stop_event().subscribe([node, queue] {
        queue([node] {
            if (node)
                emit node->dagTimerStop();
        });
    }));
    qt_connections_.emplace_back(dag_service->transaction_cache().self_transaction_event().subscribe(
        [node, queue](const Transaction& transaction, StatusTrx::StatusTrxType status) {
            queue([node, transaction, status] {
                if (node)
                    emit node->selfTxAdded(transaction, status);
            });
        }));

    const auto bridge_transaction = [this, node, queue](Dag::TransactionEvent& event, auto signal) {
        qt_connections_.emplace_back(
            event.subscribe([node, queue, signal](SectionId section, const std::string& hash) {
                queue([node, signal, section, hash] {
                    if (node)
                        emit(node->*signal)(section, hash);
                });
            }));
    };
    bridge_transaction(dag_service->transaction_sent_event(), &ExtraChainNode::dagTxSended);
    bridge_transaction(dag_service->transaction_approved_event(), &ExtraChainNode::dagTxApproved);
    bridge_transaction(dag_service->transaction_rejected_event(), &ExtraChainNode::dagTxNotApproved);

    qt_connections_.emplace_back(dag_service->control_started_event().subscribe([node, queue] {
        queue([node] {
            if (node)
                emit node->dagControlStarted();
        });
    }));
    qt_connections_.emplace_back(dag_service->control_ended_event().subscribe([node, queue] {
        queue([node] {
            if (node)
                emit node->dagControlEnded();
        });
    }));
    qt_connections_.emplace_back(dag_service->control_progress_event().subscribe([node, queue](SectionId section) {
        queue([node, section] {
            if (node)
                emit node->dagControlProgress(section);
        });
    }));
    qt_connections_.emplace_back(dag_service->control_search_started_event().subscribe([node, queue] {
        queue([node] {
            if (node)
                emit node->dagSearchControlStarted();
        });
    }));
    qt_connections_.emplace_back(dag_service->control_search_ended_event().subscribe([node, queue] {
        queue([node] {
            if (node)
                emit node->dagSearchControlEnded();
        });
    }));

    qt_connections_.emplace_back(chat_service->chats_loaded_event().subscribe([node, queue] {
        queue([node] {
            if (node)
                emit node->chatsLoaded();
        });
    }));
    qt_connections_.emplace_back(chat_service->chat_added_event().subscribe([node, queue](const Chat::Chat& chat) {
        queue([node, chat] {
            if (node)
                emit node->chatAdded(chat);
        });
    }));
    qt_connections_.emplace_back(
        chat_service->chat_updated_event().subscribe([node, queue](const Chat::Chat& chat) {
            queue([node, chat] {
                if (node)
                    emit node->chatUpdated(chat);
            });
        }));
    qt_connections_.emplace_back(chat_service->message_added_event().subscribe(
        [node, queue](const ActorId& owner_id, const std::string& file_id, const Chat::Message& message) {
            queue([node, owner_id, file_id, message] {
                if (node)
                    emit node->messageAdded(owner_id, file_id, message);
            });
        }));
    qt_connections_.emplace_back(chat_service->message_removed_event().subscribe(
        [node, queue](const ActorId& owner_id, const std::string& file_id, const std::string& message_id) {
            queue([node, owner_id, file_id, message_id] {
                if (node)
                    emit node->messageRemoved(owner_id, file_id, message_id);
            });
        }));
}

ExtraChainNodeWrapper::ExtraChainNodeWrapper(QObject*                      parent,
                                             bool                          is_client_application,
                                             bool                          is_custom_app,
                                             std::uint16_t                 ws_port,
                                             std::optional<RuntimeProfile> runtime_profile)
    : QObject(parent)
    , node(new ExtraChainNode(is_client_application, is_custom_app, ws_port, runtime_profile)) {
}

ExtraChainNodeWrapper::~ExtraChainNodeWrapper() {
    if (init_future_.valid()) {
        init_future_.wait();
    }
    delete std::exchange(node, nullptr);
}

void ExtraChainNodeWrapper::init(bool make_async) {
    if (!make_async) {
        node->process();
        return;
    }

    auto completed = std::make_shared<std::promise<void>>();
    init_future_   = completed->get_future();
    boost::asio::post(node->serial_executor(), [target = node, completed] {
        try {
            target->process();
            completed->set_value();
        } catch (...) {
            completed->set_exception(std::current_exception());
        }
    });
}
