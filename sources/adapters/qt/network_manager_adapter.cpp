/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "adapters/qt/network_manager_adapter.h"

#include <QMetaObject>
#include <QNetworkProxy>
#include <QPointer>

#include "network/network_manager.h"

QtNetworkManagerAdapter::QtNetworkManagerAdapter(NetworkManager& manager) {
    const QPointer<NetworkManager> target(&manager);
    const auto                     queue = [target](auto handler) {
        if (target) {
            QMetaObject::invokeMethod(target, std::move(handler), Qt::QueuedConnection);
        }
    };

    QObject::connect(
        &manager,
        &NetworkManager::connect_to_node,
        &manager,
        [&manager](const QString& ip, Network::Protocol protocol, bool request, bool is_constant, bool is_light) {
            manager.request_connection(ip.toStdString(), protocol, request, is_constant, is_light);
        },
        Qt::QueuedConnection);

    connections_.emplace_back(manager.socket_activated_event().subscribe(
        [target, queue](const std::string& ip, const std::string& identifier) {
            queue([target, ip, identifier] {
                if (target) {
                    emit target->newSocketActivatedWithParams(ip, identifier);
                }
            });
        }));
    connections_.emplace_back(manager.socket_ready_event().subscribe([target, queue] {
        queue([target] {
            if (target) {
                emit target->newSocketActivated();
            }
        });
    }));
    connections_.emplace_back(manager.connection_state_event().subscribe([target, queue](bool online, int count) {
        queue([target, online, count] {
            if (target) {
                emit target->connectionStatusChanged(online);
                emit target->connectionsCountChanged(count);
            }
        });
    }));
    connections_.emplace_back(
        manager.connection_error_event().subscribe([target, queue](Network::SocketServiceError error,
                                                                   const std::string&          ip,
                                                                   const std::string&          identifier,
                                                                   const std::string&          detail) {
            queue([target, error, ip, identifier, detail] {
                if (target) {
                    emit target->connectionError(error,
                                                 QString::fromStdString(ip),
                                                 QString::fromStdString(identifier),
                                                 QString::fromStdString(detail));
                }
            });
        }));
    connections_.emplace_back(manager.custom_message_event().subscribe(
        [target, queue](const NetworkPackageStorage& package, const CustomMessage& message) {
            queue([target, package, message] {
                if (target) {
                    emit target->customMessageReceived(package, message);
                }
            });
        }));
}

void NetworkManager::setup_proxy(QNetworkProxy::ProxyType type,
                                 const QString&           host_name,
                                 std::uint16_t            port,
                                 const QString&           user,
                                 const QString&           password) {
    QNetworkProxy proxy;
    proxy.setType(type);
    proxy.setHostName(host_name);
    proxy.setPort(port);
    proxy.setUser(user);
    proxy.setPassword(password);
    QNetworkProxy::setApplicationProxy(proxy);
}

void NetworkManager::remove_connection(const QString& identifier) {
    disconnect_peer(identifier.toStdString());
}

void NetworkManager::connect_to_endpoint(const QString& ip,
                                         std::uint16_t  port,
                                         bool           request_list_nodes,
                                         bool           is_constant,
                                         bool           is_light) {
    request_endpoint(ip.toStdString(), port, request_list_nodes, is_constant, is_light);
}

QString NetworkManager::local_ip() {
    return QString::fromStdString(local_ip_value());
}

QString NetworkManager::public_ip_() {
    return QString::fromStdString(public_ip());
}
