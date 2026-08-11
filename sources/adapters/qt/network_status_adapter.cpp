/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "adapters/qt/network_status_adapter.h"

QtNetworkStatusAdapter::QtNetworkStatusAdapter(ExtraChain::Core::NetworkStatus& status)
    : status_(status) {
    auto* network_information = QNetworkInformation::instance();
    if (network_information == nullptr) {
        status_.update(ExtraChain::Core::NetworkStatus::Status::Unknown);
        return;
    }

    on_reachability_changed(network_information->reachability());
    connect(network_information,
            &QNetworkInformation::reachabilityChanged,
            this,
            &QtNetworkStatusAdapter::on_reachability_changed);
}

void QtNetworkStatusAdapter::on_reachability_changed(QNetworkInformation::Reachability reachability) {
    using Status = ExtraChain::Core::NetworkStatus::Status;
    switch (reachability) {
    case QNetworkInformation::Reachability::Unknown:
        status_.update(Status::Unknown);
        break;
    case QNetworkInformation::Reachability::Disconnected:
        status_.update(Status::Offline);
        break;
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        status_.update(Status::Local);
        break;
    case QNetworkInformation::Reachability::Online:
        status_.update(Status::Online);
        break;
    }
}
