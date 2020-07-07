/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#include "managers/coin_service.h"

CoinService::CoinService(/*ResolverService *resolverService*/)
{
    //	resolver = resolverService;
}

CoinService::~CoinService()
{
    //
}

void CoinService::run()
{
    this->active = true;
    exec();
}

int CoinService::exec()
{
    while (isActive())
    {
        //
    }
    return 0;
}

void CoinService::quit()
{
    active = false;
}

bool CoinService::isActive()
{
    return active;
}

BigNumber CoinService::getPeerLimit()
{
    return PEER_LIM;
}

BigNumber CoinService::getGasLimit()
{
    return GAS_LIM;
}

const BigNumber CoinService::getGenesisAddress()
{
    return GEN_ADDR;
}

void CoinService::checkGenBlock(Block block)
{
    if (block.getApprover() == GEN_ADDR)
    {
        emit GenBlock(block);
    }
}

void CoinService::mineCoin(BigNumber blockCountLocal, BigNumber blockCountMax, int peerCount, Transaction tx)
{
    BigNumber pCount(peerCount);
    BigNumber txGas(tx.getGas());
    if ((pCount < PEER_LIM) || (txGas < GAS_LIM))
    {
        BigNumber limit = (blockCountMax + PEER_LIM + GAS_LIM) / txGas;
        BigNumber x = (blockCountMax - blockCountLocal) + (PEER_LIM - pCount) + (GAS_LIM - txGas);
        if (x < limit)
        {
            emit CoinMined(tx);
        }
    }
}
