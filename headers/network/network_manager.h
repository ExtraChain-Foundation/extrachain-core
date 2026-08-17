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

#include <memory>

#include <QObject>
#include <QtNetwork/QNetworkProxy>

#include "adapters/qt/qt_compat_global.h"
#include "network/network_service.h"

class QtNetworkManagerAdapter;

/**
 * Qt compatibility facade for NetworkService.
 *
 * NetworkService owns the network state. This class only keeps the Qt API
 * used by existing clients.
 */
class EXTRACHAIN_QT_EXPORT NetworkManager final : public QObject, public NetworkService {
    Q_OBJECT

public:
    explicit NetworkManager(ExtraChain::Core::ExtraChainNode* node,
                            ExtraChain::Core::NetworkRuntime& runtime,
                            std::uint16_t                     port,
                            QObject*                          parent = nullptr);
    ~NetworkManager() override;

public slots:
    void remove_connection(const QString& identifier);
    void connect_to_endpoint(const QString& ip,
                             std::uint16_t  port,
                             bool           request_list_nodes = false,
                             bool           is_constant        = false,
                             bool           is_light           = false);
    void setup_proxy(QNetworkProxy::ProxyType type,
                     const QString&           host_name,
                     std::uint16_t            port,
                     const QString&           user,
                     const QString&           password);

    [[nodiscard]] QString                     local_ip();
    [[nodiscard]] QString                     public_ip_();

signals:
    void finished();
    void connect_to_node(const QString&    ip,
                         Network::Protocol protocol,
                         bool              request     = false,
                         bool              is_constant = false,
                         bool              is_light    = false);
    void newSocketActivated();
    void newSocketActivatedWithParams(std::string ip, std::string identifier);
    void connectionStatusChanged(bool status);
    void connectionsCountChanged(int sockets_count);
    void connectionError(Network::SocketServiceError error, QString ip, QString identifier, QString error_data);
    void messageCountReceived(SectionId count);
    void customMessageReceived(NetworkPackageStorage package_data, CustomMessage custom_package);

private:
    std::unique_ptr<QtNetworkManagerAdapter> adapter_;
};
