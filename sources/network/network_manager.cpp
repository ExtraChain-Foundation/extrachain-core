/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "network/network_manager.h"

#include "adapters/qt/network_manager_adapter.h"

NetworkManager::NetworkManager(ExtraChain::Core::ExtraChainNode* node,
                               ExtraChain::Core::NetworkRuntime& runtime,
                               std::uint16_t                     port,
                               QObject*                          parent)
    : QObject(parent)
    , NetworkService(node, runtime, port)
    , adapter_(std::make_unique<QtNetworkManagerAdapter>(*this)) {
}

NetworkManager::~NetworkManager() = default;
