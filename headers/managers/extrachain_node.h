/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <future>
#include <memory>
#include <optional>
#include <vector>

#include <QObject>
#include <QString>
#include <boost/signals2/connection.hpp>

#include "adapters/qt/qt_compat_global.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_controller.h"
#include "network/network_manager.h"

class EXTRACHAIN_QT_EXPORT ExtraChainNode : public QObject, public ExtraChain::Core::ExtraChainNode {
    Q_OBJECT

public:
    ~ExtraChainNode() override;

    void process();
    void cleanUp();

    [[nodiscard]] NetworkManager*             network() const override;
    [[nodiscard]] DfsController*              dfs() const override;
    [[nodiscard]] std::pair<QString, QString> init_public_ip_and_country() const;

signals:
    void initNode();
    void finished();
    void nodeInitialised();
    void ready();
    void pushNotification(QString actor_id, Notification notification);
    void subscriptionAdded(ActorId owner_id, std::string file_id);
    void selfTxAdded(const Transaction& transaction, StatusTrx::StatusTrxType status);
    void dagStatus(DagStatus status);
    void dagSyncStart(SectionId from, SectionId to);
    void dagSyncProgress(SectionId section);
    void dagSyncFinish();
    void dagTimerStart(int delay_ms = 15000);
    void dagTimerStop();
    void dagTxSended(SectionId section_id, std::string hash);
    void dagTxApproved(SectionId section_id, std::string hash);
    void dagTxNotApproved(SectionId section_id, std::string hash);
    void dagControlStarted();
    void dagControlEnded();
    void dagControlProgress(SectionId section);
    void dagSearchControlStarted();
    void dagSearchControlEnded();
    void chatsLoaded();
    void chatAdded(Chat::Chat chat);
    void chatUpdated(Chat::Chat chat);
    void messageAdded(ActorId owner_id, std::string file_id, Chat::Message message);
    void messageRemoved(ActorId owner_id, std::string file_id, std::string message_id);
    void actorRenamedLoaded();
    void actorRenamed(ActorId actor_id, std::string name);
    void runtimeActivityChanged(RuntimeActivity activity);

public slots:
    void notificationToken(QString os, QString actor_id, QString token);

protected:
    std::unique_ptr<DfsService>     create_dfs_service() override;
    std::unique_ptr<NetworkService> create_network_service() override;

private:
    ExtraChainNode(bool                          is_client_application,
                   bool                          is_custom_app,
                   std::uint16_t                 port,
                   std::optional<RuntimeProfile> runtime_profile,
                   std::string                   bind_address);

    void bridge_node_events();
    void bridge_service_events();

    std::vector<boost::signals2::scoped_connection> qt_connections_;

    friend class ExtraChainNodeWrapper;
};

class EXTRACHAIN_QT_EXPORT ExtraChainNodeWrapper : public QObject {
    Q_OBJECT

public:
    ExtraChainNodeWrapper(QObject*                      parent,
                          bool                          is_client_application = false,
                          bool                          is_custom_app         = false,
                          std::uint16_t                 ws_port               = 17593,
                          std::optional<RuntimeProfile> runtime_profile       = std::nullopt,
                          std::string                   bind_address          = {});
    ~ExtraChainNodeWrapper() override;

    void init(bool make_async = false);

    ExtraChainNode* node = nullptr;

private:
    std::future<void> init_future_;
};
