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

#include <QObject>
#include <QtNetwork/QNetworkInformation>

#include "network/network_status.h"

class QtNetworkStatusAdapter final : public QObject {
public:
    explicit QtNetworkStatusAdapter(ExtraChain::Core::NetworkStatus& status);

private:
    void                             on_reachability_changed(QNetworkInformation::Reachability reachability);
    ExtraChain::Core::NetworkStatus& status_;
};
