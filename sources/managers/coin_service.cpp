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
