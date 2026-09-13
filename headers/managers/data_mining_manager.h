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

#pragma once

#include <string>
#include "dfs/dfs_service.h"
#include "utils/bignumber_float.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_utils.h"

static const int MINING_TIMER_TICK = 60000;

class DataMiningManager {
public:
    explicit DataMiningManager(ExtraChain::Core::ExtraChainNode* node);
    void request_reward();
    bool network_request_coin_reward(const Dfs::Reward::RequestReward& request, const Responder& responder);

private:
    ExtraChain::Core::ExtraChainNode* node;
};
