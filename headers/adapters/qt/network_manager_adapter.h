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

#include <vector>

#include <boost/signals2/connection.hpp>

class NetworkManager;

class QtNetworkManagerAdapter final {
public:
    explicit QtNetworkManagerAdapter(NetworkManager& manager);

    QtNetworkManagerAdapter(const QtNetworkManagerAdapter&)            = delete;
    QtNetworkManagerAdapter& operator=(const QtNetworkManagerAdapter&) = delete;

private:
    std::vector<boost::signals2::scoped_connection> connections_;
};
