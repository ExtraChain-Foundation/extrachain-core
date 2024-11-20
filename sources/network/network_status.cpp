/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "network/network_status.h"

#include "utils/exc_logs.h"

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent) {
    auto networkInfo = QNetworkInformation::instance();
    if (networkInfo == nullptr) {
        eLog("[NetworkStatus] Can't detect network status");
        setNetworkStatus(Status::Unknown);
        return;
    }
    onReachabilityChanged(networkInfo->reachability());
    connect(networkInfo, &QNetworkInformation::reachabilityChanged, this,
            &NetworkStatus::onReachabilityChanged);
}

NetworkStatus::Status NetworkStatus::status() {
    return m_networkStatus;
}

void NetworkStatus::onReachabilityChanged(QNetworkInformation::Reachability reachability) {
    switch (reachability) {
    case QNetworkInformation::Reachability::Unknown:
    case QNetworkInformation::Reachability::Disconnected:
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        setNetworkStatus(Status::Offline);
        break;
    case QNetworkInformation::Reachability::Online:
        setNetworkStatus(Status::Online);
        break;
    }
}

void NetworkStatus::setNetworkStatus(NetworkStatus::Status status) {
    if (m_networkStatus == status) {
        return;
    }

    m_networkStatus = status;
    emit statusChanged(status);
}
